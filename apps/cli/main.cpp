// sillok: command-line app over the sillok C ABI.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "paths.hpp"
#include "process.hpp"
#include "progress.hpp"
#include "sillok/sillok.h"
#include "wav_reader.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace sillok::cli {
namespace {

using Json = nlohmann::json;

constexpr int kExitError = 1;
constexpr int kExitUsage = 2;
constexpr int kExitInterrupted = 130;

constexpr const char* kUsage = R"(usage:
  sillok transcribe INPUT [options]
      --language auto|ko|en   spoken language (default auto)
      --diarize               label speakers
      --term TEXT             glossary term for the model (repeatable)
      --glossary FILE         glossary terms, one per line
      --format txt|srt|json   output format (default txt)
      --output FILE           write the result here instead of stdout
      --speaker-label TEXT    speaker label, {n} is the number
      --device cpu|gpu        compute device (default cpu)
      --gpu-index N           which GPU (default 0)
      --threads N             CPU threads (default: automatic)
      --model ID              transcription model (default: the recommended one)
      --models DIR            model directory
      --quiet                 no progress output
  sillok export TRANSCRIPT.json --format txt|srt|json [--output FILE]
                [--speaker-label TEXT]
  sillok models list|check [--models DIR]
  sillok models download [--model ID] [--models DIR]
  sillok devices
  sillok --version

Models are looked up in --models, else $SILLOK_MODELS, else {models}.
WAV files at 16 kHz mono are read directly; other formats need ffmpeg.
SILLOK_LOG=1 prints the model libraries' logs.
)";

std::string Usage() {
  std::string usage = kUsage;
  usage.replace(usage.find("{models}"), 8, PathToUtf8(DefaultModelsDir()));
  return usage;
}

class UsageError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

void Check(sillok_status status, const char* what) {
  if (status != SILLOK_OK) {
    throw std::runtime_error(std::string(what) + ": " + sillok_last_error());
  }
}

std::string Take(char* text) {
  std::string copy = text ? text : "";
  sillok_free_string(text);
  return copy;
}

struct EngineDeleter {
  void operator()(sillok_engine* engine) const { sillok_engine_destroy(engine); }
};
struct JobDeleter {
  void operator()(sillok_job* job) const { sillok_job_destroy(job); }
};
using Engine = std::unique_ptr<sillok_engine, EngineDeleter>;
using Job = std::unique_ptr<sillok_job, JobDeleter>;

// Parsed "--name value" options and positional arguments.
struct Arguments {
  std::vector<std::string> positional;
  std::vector<std::pair<std::string, std::string>> options;
  std::vector<std::string> flags;

  std::optional<std::string> Get(const std::string& name) const {
    std::optional<std::string> value;
    for (const auto& [key, v] : options) {
      if (key == name) value = v;
    }
    return value;
  }
  std::vector<std::string> All(const std::string& name) const {
    std::vector<std::string> values;
    for (const auto& [key, v] : options) {
      if (key == name) values.push_back(v);
    }
    return values;
  }
  bool Has(const std::string& flag) const {
    for (const std::string& f : flags) {
      if (f == flag) return true;
    }
    return false;
  }
};

Arguments Parse(const std::vector<std::string>& args, std::size_t first,
                const std::vector<std::string>& flag_names) {
  Arguments parsed;
  for (std::size_t i = first; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (!arg.starts_with("--")) {
      parsed.positional.push_back(arg);
      continue;
    }
    const std::string name = arg.substr(2);
    if (std::find(flag_names.begin(), flag_names.end(), name) != flag_names.end()) {
      parsed.flags.push_back(name);
    } else if (i + 1 < args.size()) {
      parsed.options.emplace_back(name, args[++i]);
    } else {
      throw UsageError("missing value for " + arg);
    }
  }
  return parsed;
}

std::filesystem::path ModelsDir(const Arguments& args) {
  if (const auto dir = args.Get("models")) return PathFromUtf8(*dir);
  if (const char* env = std::getenv("SILLOK_MODELS"); env != nullptr && *env) {
    return PathFromUtf8(env);
  }
  return DefaultModelsDir();
}

std::FILE* OpenFile(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
  const std::wstring wide_mode(mode, mode + std::strlen(mode));
  return _wfopen(path.c_str(), wide_mode.c_str());
#else
  return std::fopen(path.c_str(), mode);
#endif
}

void WriteOutput(const std::string& text, const std::optional<std::string>& output) {
  if (!output) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
    return;
  }
  std::ofstream out(PathFromUtf8(*output), std::ios::binary);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!out) throw std::runtime_error("cannot write " + *output);
}

// Set by SIGINT/SIGTERM; a watcher thread turns it into a cancellation.
volatile std::sig_atomic_t interrupted = 0;

extern "C" void OnInterrupt(int) { interrupted = 1; }

// --- models ----------------------------------------------------------------

Json ModelStatus(const std::filesystem::path& dir, bool verify_hash) {
  char* out = nullptr;
  Check(sillok_model_check(PathToUtf8(dir).c_str(), verify_hash ? 1 : 0, &out),
        "model check");
  return Json::parse(Take(out));
}

std::string StatusOf(const Json& statuses, const std::string& file) {
  for (const Json& entry : statuses) {
    if (entry["file"] == file) return entry["status"];
  }
  return "missing";
}

void Extract(const std::filesystem::path& archive, const std::filesystem::path& dir) {
#if defined(__APPLE__)
  Process unzip({"ditto", "-x", "-k", PathToUtf8(archive), PathToUtf8(dir)}, false);
#elif defined(_WIN32)
  Process unzip({"tar", "-xf", PathToUtf8(archive), "-C", PathToUtf8(dir)}, false);
#else
  Process unzip({"unzip", "-q", "-o", PathToUtf8(archive), "-d", PathToUtf8(dir)}, false);
#endif
  if (unzip.Wait() != 0) throw std::runtime_error("cannot extract " + PathToUtf8(archive));
}

// The models an engine with transcription model `id` uses (empty for the
// recommended one).
Json EngineModels(const std::string& id) {
  char* out = nullptr;
  const sillok_status status = sillok_engine_models(id.c_str(), &out);
  if (status == SILLOK_INVALID_ARGUMENT) {
    throw UsageError("unknown model: " + id + " (see sillok models list)");
  }
  Check(status, "models");
  return Json::parse(Take(out));
}

int RunModels(const std::vector<std::string>& all_args) {
  const Arguments args = Parse(all_args, 2, {});
  if (args.positional.size() != 1) throw UsageError("models needs list, download or check");
  const std::string action = args.positional[0];
  const std::filesystem::path dir = ModelsDir(args);
  char* out = nullptr;
  Check(sillok_model_catalog(&out), "model catalog");
  const Json catalog = Json::parse(Take(out));

  if (action == "list" || action == "check") {
    const bool verify = action == "check";
    const Json statuses = std::filesystem::is_directory(dir) ? ModelStatus(dir, verify) : Json::array();
    std::printf("models in %s (* recommended)\n", PathToUtf8(dir).c_str());
    bool recommended_ok = true;
    for (const Json& model : catalog) {
      double size = 0;
      std::string status = "ok";
      for (const Json& file : model["files"]) {
        size += file["size"].get<double>();
        const std::string file_status = StatusOf(statuses, file["file"]);
        if (!file["zip"].get<bool>() && file_status != "ok") status = file_status;
      }
      const bool recommended = model["recommended"].get<bool>();
      recommended_ok = recommended_ok && (!recommended || status == "ok");
      std::printf("  %s %-30s %-16s %8.1f MB  %s\n", recommended ? "*" : " ",
                  model["id"].get<std::string>().c_str(), model["role"].get<std::string>().c_str(),
                  size / 1e6, status.c_str());
    }
    return recommended_ok ? 0 : kExitError;
  }
  if (action != "download") throw UsageError("unknown models action: " + action);

  std::filesystem::create_directories(dir);
  std::vector<std::string> done;  // files are shared between models
  for (const Json& model : EngineModels(args.Get("model").value_or(""))) {
    for (const Json& entry : model["files"]) {
      const std::string file = entry["file"];
      if (std::find(done.begin(), done.end(), file) != done.end()) continue;
      done.push_back(file);
      const std::filesystem::path path = dir / PathFromUtf8(file);
      std::string status = StatusOf(ModelStatus(dir, false), file);
      if (status == "ok") {
        std::printf("%s: present\n", file.c_str());
        continue;
      }
      if (status != "extract") {
        std::printf("%s: downloading %.1f MB\n", file.c_str(), entry["size"].get<double>() / 1e6);
        std::fflush(stdout);
        std::filesystem::path part = path;
        part += ".part";
        // -C - resumes an interrupted download.
        Process curl({"curl", "-L", "--fail", "--retry", "3", "-C", "-",
                      StderrIsTerminal() ? "--progress-bar" : "--no-progress-meter",
                      "-o", PathToUtf8(part), entry["url"].get<std::string>()},
                     false);
        if (curl.Wait() != 0) throw std::runtime_error("download of " + file + " failed");
        std::filesystem::rename(part, path);
      }
      status = StatusOf(ModelStatus(dir, true), file);
      if (status != "ok" && status != "extract") {
        std::filesystem::remove(path);
        throw std::runtime_error(file + " failed verification (" + status + "); removed");
      }
      if (status == "extract") {
        Extract(path, dir);
        if (StatusOf(ModelStatus(dir, false), file) != "ok") {
          throw std::runtime_error("extracting " + file + " did not produce the model");
        }
        std::filesystem::remove(path);
      }
      std::printf("%s: verified\n", file.c_str());
    }
  }
  return 0;
}

// --- devices -----------------------------------------------------------------

int RunDevices() {
  char* out = nullptr;
  Check(sillok_devices(&out), "devices");
  for (const Json& device : Json::parse(Take(out))) {
    std::printf("%-10s %-6s %s", device["name"].get<std::string>().c_str(),
                device["type"].get<std::string>().c_str(),
                device["description"].get<std::string>().c_str());
    const auto total = device["memory_total"].get<double>();
    if (total > 0) std::printf("  (%.1f GB)", total / 1e9);
    std::printf("\n");
  }
  return 0;
}

// --- export ------------------------------------------------------------------

std::string ExportOptions(const Arguments& args) {
  Json options = Json::object();
  if (const auto label = args.Get("speaker-label")) options["speaker_label"] = *label;
  return options.dump();
}

int RunExport(const std::vector<std::string>& all_args) {
  const Arguments args = Parse(all_args, 2, {});
  if (args.positional.size() != 1) throw UsageError("export needs one transcript file");
  const auto path = FindFile(args.positional[0]);
  if (!path) throw std::runtime_error("file not found: " + args.positional[0]);
  std::ifstream in(*path, std::ios::binary);
  std::stringstream document;
  document << in.rdbuf();
  char* out = nullptr;
  Check(sillok_export(document.str().c_str(), args.Get("format").value_or("txt").c_str(),
                      ExportOptions(args).c_str(), &out),
        "export");
  WriteOutput(Take(out), args.Get("output"));
  return 0;
}

// --- transcribe --------------------------------------------------------------

// Feeds a WAV stream into `job` in one-second pieces.
void PushWav(WavReader& wav, sillok_job* job) {
  const auto frames = static_cast<std::size_t>(wav.sample_rate());
  while (true) {
    if (interrupted) return;
    const std::vector<float> samples = wav.Read(frames);
    if (samples.empty()) return;
    Check(sillok_job_push_audio(job, samples.data(),
                                samples.size() / static_cast<std::size_t>(wav.channels()),
                                wav.sample_rate(), wav.channels()),
          "audio");
  }
}

// 16 kHz mono WAV is read here; anything else is decoded by ffmpeg, which
// keeps the original rate and channels (the library resamples).
void PushRecording(const std::filesystem::path& path, sillok_job* job) {
  if (std::FILE* file = OpenFile(path, "rb")) {
    const std::unique_ptr<std::FILE, int (*)(std::FILE*)> closer(file, std::fclose);
    std::optional<WavReader> wav;
    try {
      wav.emplace(file);
    } catch (const std::runtime_error&) {
    }
    if (wav && wav->sample_rate() == 16000 && wav->channels() == 1) {
      PushWav(*wav, job);
      return;
    }
  } else {
    throw std::runtime_error("cannot open " + PathToUtf8(path));
  }
  std::optional<Process> ffmpeg;
  try {
    ffmpeg.emplace(std::vector<std::string>{"ffmpeg", "-nostdin", "-hide_banner", "-loglevel",
                                            "error", "-i", PathToUtf8(path), "-vn", "-f", "wav",
                                            "-acodec", "pcm_f32le", "-"},
                   true);
  } catch (const std::runtime_error&) {
    throw std::runtime_error("ffmpeg is needed to read " + PathToUtf8(path) +
                             " (only 16 kHz mono WAV is read without it)");
  }
  WavReader wav(ffmpeg->output());
  PushWav(wav, job);
  if (!interrupted && ffmpeg->Wait() != 0) {
    throw std::runtime_error("ffmpeg could not decode " + PathToUtf8(path));
  }
}

std::vector<std::string> GlossaryTerms(const Arguments& args) {
  std::vector<std::string> terms = args.All("term");
  if (const auto file = args.Get("glossary")) {
    const auto path = FindFile(*file);
    if (!path) throw std::runtime_error("glossary not found: " + *file);
    std::ifstream in(*path, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!line.empty()) terms.push_back(line);
    }
  }
  return terms;
}

int RunTranscribe(const std::vector<std::string>& all_args) {
  const Arguments args = Parse(all_args, 2, {"diarize", "quiet"});
  if (args.positional.size() != 1) throw UsageError("transcribe needs one input file");
  const auto input = FindFile(args.positional[0]);
  if (!input) throw std::runtime_error("file not found: " + args.positional[0]);
  const std::string format = args.Get("format").value_or("txt");
  if (format != "txt" && format != "srt" && format != "json") {
    throw UsageError("unknown format: " + format);
  }

  Json config = {
      {"models_dir", PathToUtf8(ModelsDir(args))},
      {"device", args.Get("device").value_or("cpu")},
      {"gpu_index", std::stoi(args.Get("gpu-index").value_or("0"))},
      {"threads", std::stoi(args.Get("threads").value_or("0"))}};
  if (const auto model = args.Get("model")) config["transcription_model"] = *model;
  sillok_engine* raw_engine = nullptr;
  Check(sillok_engine_create(config.dump().c_str(), &raw_engine), "engine");
  const Engine engine(raw_engine);

  const Json options = {{"language", args.Get("language").value_or("auto")},
                        {"diarize", args.Has("diarize")},
                        {"glossary", GlossaryTerms(args)}};
  sillok_job* raw_job = nullptr;
  Check(sillok_job_create(engine.get(), options.dump().c_str(), &raw_job), "job");
  const Job job(raw_job);

  std::signal(SIGINT, OnInterrupt);
  std::signal(SIGTERM, OnInterrupt);
  std::atomic<bool> done = false;
  std::thread watcher([&] {
    while (!done) {
      if (interrupted) {
        sillok_job_cancel(job.get());
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  });
  const auto stop_watcher = [&] {
    done = true;
    watcher.join();
  };

  ProgressPrinter progress(args.Has("quiet"));
  char* out = nullptr;
  sillok_status status = SILLOK_OK;
  try {
    PushRecording(*input, job.get());
    status = sillok_job_run(
        job.get(),
        [](void* user, sillok_stage stage, double fraction) {
          static_cast<ProgressPrinter*>(user)->Update(stage, fraction);
        },
        &progress, &out);
  } catch (...) {
    stop_watcher();
    throw;
  }
  stop_watcher();
  progress.Finish();
  if (status == SILLOK_CANCELLED || interrupted) {
    sillok_free_string(out);
    std::fprintf(stderr, "cancelled\n");
    return kExitInterrupted;
  }
  Check(status, "transcription");
  const std::string transcript = Take(out);

  std::string text = transcript;
  if (format != "json") {
    char* rendered = nullptr;
    Check(sillok_export(transcript.c_str(), format.c_str(), ExportOptions(args).c_str(), &rendered),
          "export");
    text = Take(rendered);
  }
  WriteOutput(text, args.Get("output"));
  return 0;
}

int Main(int argc, char** argv) {
  const std::vector<std::string> args = Utf8Arguments(argc, argv);
  try {
    const std::string command = args.size() > 1 ? args[1] : "";
    if (command == "transcribe") return RunTranscribe(args);
    if (command == "export") return RunExport(args);
    if (command == "models") return RunModels(args);
    if (command == "devices") return RunDevices();
    if (command == "--version") {
      std::printf("%s\n", sillok_version());
      return 0;
    }
    if (command == "--help" || command == "-h") {
      std::fputs(Usage().c_str(), stdout);
      return 0;
    }
    throw UsageError(command.empty() ? "no command" : "unknown command: " + command);
  } catch (const UsageError& error) {
    std::fprintf(stderr, "sillok: %s\n\n%s", error.what(), Usage().c_str());
    return kExitUsage;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "sillok: %s\n", error.what());
    return kExitError;
  }
}

}  // namespace
}  // namespace sillok::cli

int main(int argc, char** argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
  return sillok::cli::Main(argc, argv);
}
