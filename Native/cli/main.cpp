#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>

#include "job.hpp"
#include "manifest_data.hpp"
#include "models.hpp"
#include "process.hpp"
#include "segments.hpp"
#include "transcriber.hpp"
#include "wav.hpp"
#include "whisper_engine.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;
using namespace sillok;

namespace {

constexpr const char* kUsage = R"(사용법:
  sillok transcribe <오디오> [--lang auto|koen|<코드>] [--terms 용어1,용어2] [--diarize]
                    [--format txt|srt|json] [--out 파일] [--checkpoint 파일] [--cpu] [--quiet]
  sillok models [download] [--diarize]
  sillok info

모델 위치는 --models <폴더> 또는 SILLOK_MODELS 환경 변수로 바꿀 수 있습니다.
WAV가 아닌 오디오는 ffmpeg로, 모델은 curl로 받습니다.
)";

#ifdef __APPLE__
constexpr bool kCoreML = true;
#else
constexpr bool kCoreML = false;
#endif

CancelToken interrupt;

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Arguments {
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;
    std::set<std::string> flags;

    static Arguments parse(const std::vector<std::string>& arguments) {
        static const std::set<std::string> known = {"--diarize", "--cpu", "--quiet"};
        Arguments result;
        for (size_t i = 0; i < arguments.size(); ++i) {
            if (known.count(arguments[i])) {
                result.flags.insert(arguments[i]);
            } else if (arguments[i].rfind("--", 0) == 0 && i + 1 < arguments.size()) {
                result.options[arguments[i]] = arguments[i + 1];
                ++i;
            } else {
                result.positional.push_back(arguments[i]);
            }
        }
        return result;
    }

    std::optional<std::string> value(const std::string& name) const {
        auto it = options.find(name);
        return it == options.end() ? std::nullopt : std::optional<std::string>(it->second);
    }

    bool has(const std::string& flag) const { return flags.count(flag) > 0; }
};

struct ScratchDirectory {
    fs::path path = fs::temp_directory_path() / ("sillok-" + makeUUID());

    ScratchDirectory() { fs::create_directories(path); }
    ~ScratchDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

fs::path environmentPath(const char* name) {
#ifdef _WIN32
    const std::wstring wide(name, name + std::strlen(name));
    const wchar_t* value = _wgetenv(wide.c_str());
    return value ? fs::path(value) : fs::path();
#else
    const char* value = std::getenv(name);
    return value ? fs::path(value) : fs::path();
#endif
}

fs::path modelsDirectory(const Arguments& arguments) {
    if (auto value = arguments.value("--models")) return utf8Path(*value);
    if (auto value = environmentPath("SILLOK_MODELS"); !value.empty()) return value;
#if defined(_WIN32)
    return environmentPath("LOCALAPPDATA") / "Sillok" / "Models";
#elif defined(__APPLE__)
    return environmentPath("HOME") / "Library" / "Application Support" / "Sillok" / "Models";
#else
    const fs::path data = environmentPath("XDG_DATA_HOME");
    return (data.empty() ? environmentPath("HOME") / ".local" / "share" : data) / "sillok" / "models";
#endif
}

ModelManifest manifest() {
    return json::parse(cli::kManifestJson).get<ModelManifest>();
}

std::vector<ModelEntry> requiredModels(const ModelManifest& models, bool diarize) {
    return ModelSelection::required(models, ModelSelection::defaultWhisperId, kCoreML, diarize);
}

void download(const ModelEntry& entry, const fs::path& root) {
    const fs::path downloads = root / ".downloads";
    fs::create_directories(downloads);
    const fs::path part = downloads / utf8Path(entry.fileName + ".part");
    std::error_code error;
    const bool complete = fs::exists(part, error) && static_cast<int64_t>(fs::file_size(part, error)) == entry.size;
    int status = complete ? 0 : cli::kCommandNotFound;
    for (size_t i = 0; !complete && i < entry.urls.size(); ++i) {
        std::fprintf(stderr, "%s 받는 중\n", entry.displayName.c_str());
        status = cli::runProcess({"curl", "-fL", "--retry", "3", "-C", "-", "--progress-bar", "-o", utf8String(part), entry.urls[i]});
        if (status == cli::kCommandNotFound) throw Failure("curl을 찾을 수 없습니다. curl을 설치해 주세요");
        if (interrupt.isCancelled()) throw Cancelled();
        if (status == 0) break;
    }
    if (status != 0) throw Failure(entry.displayName + "을(를) 받지 못했습니다");
    installVerified(part, entry, root);
    if (entry.kind == ModelKind::coreMLEncoder) {
        const fs::path archive = root / utf8Path(entry.fileName);
        if (cli::runProcess({"ditto", "-x", "-k", utf8String(archive), utf8String(root)}) != 0) {
            throw Failure("Core ML 인코더 압축을 풀지 못했습니다");
        }
        fs::remove(archive);
    }
}

std::unique_ptr<WavReader> openAudio(const fs::path& input, const fs::path& scratch, bool quiet) {
    try {
        return std::make_unique<WavReader>(input);
    } catch (const std::invalid_argument&) {
    }
    const fs::path converted = scratch / "audio.wav";
    if (!quiet) std::fprintf(stderr, "ffmpeg로 16kHz 모노 WAV로 변환하는 중\n");
    const int status = cli::runProcess({"ffmpeg", "-nostdin", "-v", "error", "-y", "-i", utf8String(input), "-ac", "1",
                                        "-ar", "16000", "-c:a", "pcm_s16le", "-f", "wav", utf8String(converted)});
    if (status == cli::kCommandNotFound) {
        throw Failure("WAV가 아닌 파일을 읽으려면 ffmpeg가 필요합니다. ffmpeg를 설치하거나 16kHz 모노 WAV로 변환해 주세요");
    }
    if (interrupt.isCancelled()) throw Cancelled();
    if (status != 0) throw Failure("오디오를 변환하지 못했습니다");
    return std::make_unique<WavReader>(converted);
}

LanguageMode languageMode(const std::optional<std::string>& value) {
    if (!value || *value == "auto") return LanguageMode{};
    if (*value == "koen") return LanguageMode{LanguageMode::Kind::koreanEnglish, ""};
    return LanguageMode{LanguageMode::Kind::fixed, *value};
}

std::vector<std::string> vocabulary(const std::optional<std::string>& value) {
    std::vector<std::string> terms;
    if (!value) return terms;
    size_t start = 0;
    while (start <= value->size()) {
        const size_t comma = std::min(value->find(',', start), value->size());
        const std::string term = trimSpaces(value->substr(start, comma - start));
        if (!term.empty()) terms.push_back(term);
        start = comma + 1;
    }
    return terms;
}

const char* phaseText(JobPhase phase) {
    switch (phase) {
    case JobPhase::analyzing: return "음성 구간 분석";
    case JobPhase::transcribing: return "전사";
    case JobPhase::diarizing: return "화자 구분";
    }
    return "";
}

int transcribe(const Arguments& arguments) {
    if (arguments.positional.size() != 2) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    const fs::path input = utf8Path(arguments.positional[1]);
    if (!fs::is_regular_file(input)) throw Failure("파일이 없습니다: " + arguments.positional[1]);
    const bool diarize = arguments.has("--diarize");
    const bool quiet = arguments.has("--quiet");
    const bool useGPU = !arguments.has("--cpu");
    const fs::path root = modelsDirectory(arguments);
    const auto models = manifest();

    std::string missing;
    for (const auto& entry : requiredModels(models, diarize)) {
        if (entry.kind != ModelKind::coreMLEncoder && !isInstalled(entry, root)) missing += (missing.empty() ? "" : ", ") + entry.displayName;
    }
    if (!missing.empty()) {
        throw Failure("모델이 없습니다: " + missing + "\n`sillok models download" + (diarize ? " --diarize" : "") + "`로 받아 주세요");
    }

    ScratchDirectory scratch;
    auto audio = openAudio(input, scratch.path, quiet);
    const ModelEntry& whisper = *models.entry(ModelSelection::defaultWhisperId);
    std::optional<fs::path> encoder;
    if (const ModelEntry* entry = models.coreMLEncoder(whisper); kCoreML && useGPU && entry && isInstalled(*entry, root)) {
        encoder = root / utf8Path(entry->installedName());
    }

    const auto started = std::chrono::steady_clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(); };
    WhisperEngine engine;
    engine.load(root / utf8Path(whisper.fileName), encoder, useGPU);
    if (!quiet) std::fprintf(stderr, "오디오 %.1f초, 모델 로드 %.1f초\n", audio->duration(), elapsed());

    JobRequest request;
    request.modelId = whisper.id;
    request.options.languageMode = languageMode(arguments.value("--lang"));
    request.options.vocabulary = vocabulary(arguments.value("--terms"));
    request.options.speakerDiarization = diarize;
    request.checkpoint = arguments.value("--checkpoint") ? utf8Path(*arguments.value("--checkpoint")) : scratch.path / "checkpoint.json";
    request.duration = audio->duration();
    request.vadModel = root / utf8Path(models.entry(ModelSelection::vadId)->fileName);
    if (diarize) request.diarizationModel = root / utf8Path(models.entry(ModelSelection::diarizationId)->fileName);
    request.useGPU = useGPU;

    const auto transcribing = std::chrono::steady_clock::now();
    ProgressHandler progress;
    if (!quiet) {
        progress = [&](const JobProgress& update) {
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - transcribing).count();
            std::fprintf(stderr, "\r%s %5.1f%%  %6.1f초   ", phaseText(update.phase), update.fractionCompleted * 100, seconds);
            std::fflush(stderr);
            return 0;
        };
    }
    const auto segments = runJob(request, [&](double start, double end) { return audio->read(start, end); },
                                 engineServices(engine, request), progress, interrupt);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - transcribing).count();
    if (!quiet) {
        std::fprintf(stderr, "\n%.1f초 동안 %.1f초 처리 (%.1f배 실시간), 세그먼트 %zu개\n", seconds, audio->duration(),
                     audio->duration() / std::max(seconds, 0.001), segments.size());
    }
    if (arguments.value("--checkpoint")) fs::remove(request.checkpoint);

    const std::string format = arguments.value("--format").value_or("txt");
    std::string output;
    if (format == "txt") {
        output = exportText(segments);
    } else if (format == "srt") {
        output = exportSubRip(segments);
    } else if (format == "json") {
        output = json{{"duration", audio->duration()}, {"segments", segments}}.dump(2) + "\n";
    } else {
        throw Failure("알 수 없는 형식입니다: " + format);
    }
    if (auto path = arguments.value("--out")) {
        std::ofstream file(utf8Path(*path), std::ios::binary);
        file << output;
        if (!file) throw Failure("저장하지 못했습니다: " + *path);
        if (!quiet) std::fprintf(stderr, "저장했습니다: %s\n", path->c_str());
    } else {
        std::fwrite(output.data(), 1, output.size(), stdout);
    }
    return 0;
}

int listModels(const Arguments& arguments) {
    const fs::path root = modelsDirectory(arguments);
    const auto models = manifest();
    const auto required = requiredModels(models, arguments.has("--diarize"));
    if (arguments.positional.size() > 1 && arguments.positional[1] == "download") {
        for (const auto& entry : required) {
            if (!isInstalled(entry, root)) download(entry, root);
        }
    }
    std::printf("모델 폴더: %s\n", utf8String(root).c_str());
    for (const auto& entry : models.models) {
        if (!kCoreML && entry.kind == ModelKind::coreMLEncoder) continue;
        const bool needed = std::any_of(required.begin(), required.end(), [&](const ModelEntry& item) { return item.id == entry.id; });
        const char* state = isInstalled(entry, root) ? "설치됨" : needed ? "필요" : "선택";
        std::printf("  [%s] %s (%lldMB, %s)\n", state, entry.displayName.c_str(), static_cast<long long>(entry.size / 1048576), entry.license.c_str());
    }
    return 0;
}

int info(const Arguments& arguments) {
    std::printf("%s\n모델 폴더: %s\n", systemInfo().c_str(), utf8String(modelsDirectory(arguments)).c_str());
    return 0;
}

int run(const std::vector<std::string>& arguments) {
    std::signal(SIGINT, [](int) { interrupt.cancel(); });
    const auto parsed = Arguments::parse(arguments);
    const std::string command = parsed.positional.empty() ? "" : parsed.positional.front();
    try {
        if (command == "transcribe") return transcribe(parsed);
        if (command == "models") return listModels(parsed);
        if (command == "info") return info(parsed);
        std::fputs(kUsage, stderr);
        return 2;
    } catch (const Cancelled&) {
        std::fputs("\n취소했습니다. 같은 --checkpoint로 다시 실행하면 이어서 전사합니다\n", stderr);
        return 130;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "\n%s\n", error.what());
        return 1;
    }
}

}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) arguments.push_back(utf8String(fs::path(argv[i])));
    return run(arguments);
}
#else
int main(int argc, char** argv) {
    return run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
