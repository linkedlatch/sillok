#include "whisper_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <whisper.h>
#include <zlib.h>

namespace sillok {

namespace fs = std::filesystem;

static bool verboseLogging() {
    const char* value = std::getenv("SILLOK_WHISPER_LOG");
    return value && std::strcmp(value, "1") == 0;
}

void configureLogging() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (verboseLogging()) return;
        whisper_log_set([](ggml_log_level level, const char* text, void*) {
            if (level == GGML_LOG_LEVEL_ERROR && text) std::fputs(text, stderr);
        }, nullptr);
    });
}

std::string systemInfo() {
    configureLogging();
    std::string info = std::string("whisper.cpp ") + whisper_version() + "\nbackends:";
    for (size_t i = 0; i < ggml_backend_reg_count(); ++i) {
        info += std::string(" ") + ggml_backend_reg_name(ggml_backend_reg_get(i));
    }
    return info + "\n" + whisper_print_system_info();
}

std::string coreMLEncoderName(const std::string& ggmlFileName) {
    std::string stem = ggmlFileName;
    if (auto dot = stem.rfind('.'); dot != std::string::npos) stem = stem.substr(0, dot);
    if (auto dash = stem.rfind('-'); dash != std::string::npos) {
        std::string suffix = stem.substr(dash);
        if (suffix.size() == 5 && suffix[1] == 'q' && suffix[3] == '_') stem = stem.substr(0, dash);
    }
    return stem + "-encoder.mlmodelc";
}

std::optional<float> compressionRatio(const std::string& text) {
    if (text.empty()) return std::nullopt;
    z_stream stream{};
    if (deflateInit2(&stream, 5, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return std::nullopt;
    std::vector<unsigned char> output(deflateBound(&stream, text.size()));
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(text.data()));
    stream.avail_in = static_cast<uInt>(text.size());
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    int status = deflate(&stream, Z_FINISH);
    size_t compressed = stream.total_out;
    deflateEnd(&stream);
    if (status != Z_STREAM_END || compressed == 0) return std::nullopt;
    return static_cast<float>(text.size()) / static_cast<float>(compressed);
}

std::vector<WordTimestamp> assembleWords(const std::vector<TokenPiece>& tokens) {
    std::vector<WordTimestamp> words;
    std::vector<const TokenPiece*> current;
    auto flush = [&] {
        if (current.empty()) return;
        WordTimestamp word;
        float probability = 0;
        for (const auto* piece : current) {
            word.word += piece->bytes;
            probability += piece->probability;
        }
        word.start = current.front()->start;
        word.end = std::max(current.back()->end, word.start);
        word.probability = probability / static_cast<float>(current.size());
        words.push_back(word);
        current.clear();
    };
    for (const auto& token : tokens) {
        if (token.bytes.empty()) continue;
        if (token.bytes.front() == ' ') flush();
        current.push_back(&token);
    }
    flush();
    return words;
}

WhisperEngine::~WhisperEngine() {
    unload();
}

void WhisperEngine::load(const fs::path& ggml, const std::optional<fs::path>& coreMLEncoder, bool useGPU) {
    configureLogging();
    unload();
    fs::path staging = fs::temp_directory_path() / ("sillok-load-" + makeUUID());
    fs::create_directories(staging);
    const fs::path model = staging / ggml.filename();
    fs::create_symlink(ggml, model);
    if (coreMLEncoder) fs::create_directory_symlink(*coreMLEncoder, staging / coreMLEncoderName(ggml.filename().string()));

    whisper_context_params params = whisper_context_default_params();
    params.use_gpu = useGPU;
    whisper_context* context = whisper_init_from_file_with_params(model.string().c_str(), params);
    if (!context) {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        throw std::runtime_error("모델을 불러오지 못했습니다: " + ggml.filename().string());
    }
    context_ = context;
    staging_ = staging;
    threads_ = static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 4u));
}

void WhisperEngine::unload() {
    if (context_) whisper_free(context_);
    context_ = nullptr;
    if (!staging_.empty()) {
        std::error_code ignored;
        fs::remove_all(staging_, ignored);
        staging_.clear();
    }
}

whisper_context* WhisperEngine::require() const {
    if (!context_) throw std::runtime_error("모델이 로드되지 않았습니다");
    return context_;
}

LanguageDetection WhisperEngine::detectLanguage(const std::vector<float>& samples, const std::optional<std::set<std::string>>& allowed) {
    whisper_context* context = require();
    if (whisper_pcm_to_mel(context, samples.data(), static_cast<int>(samples.size()), threads_) != 0) {
        throw std::runtime_error("멜 스펙트로그램 계산 실패");
    }
    std::vector<float> probabilities(whisper_lang_max_id() + 1, 0.0f);
    if (whisper_lang_auto_detect(context, 0, threads_, probabilities.data()) < 0) {
        throw std::runtime_error("언어 감지 실패");
    }
    std::optional<std::pair<std::string, float>> best;
    float total = 0;
    for (int id = 0; id < static_cast<int>(probabilities.size()); ++id) {
        const char* name = whisper_lang_str(id);
        if (!name || (allowed && !allowed->count(name))) continue;
        total += probabilities[id];
        if (!best || probabilities[id] > best->second) best = std::make_pair(std::string(name), probabilities[id]);
    }
    if (!best) throw std::runtime_error("허용된 언어가 없습니다");
    return LanguageDetection{best->first, total > 0 ? static_cast<double>(best->second / total) : 0.0};
}

int WhisperEngine::tokenCount(const std::string& text) {
    std::vector<whisper_token> tokens(1024);
    int count = whisper_tokenize(require(), text.c_str(), tokens.data(), static_cast<int>(tokens.size()));
    return std::abs(count);
}

std::vector<Segment> WhisperEngine::transcribe(const std::vector<float>& samples, const ChunkTranscribeOptions& options, const CancelToken& cancel) {
    whisper_context* context = require();
    if (cancel.isCancelled()) throw Cancelled();

    whisper_full_params params = whisper_full_default_params(options.beamSize > 1 ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);
    params.n_threads = threads_;
    params.no_context = true;
    params.single_segment = false;
    params.print_special = false;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.token_timestamps = true;
    params.detect_language = false;
    params.temperature = 0;
    params.temperature_inc = 0.2f;
    params.entropy_thold = 2.4f;
    params.logprob_thold = -1.0f;
    params.no_speech_thold = 0.6f;
    params.beam_search.beam_size = options.beamSize;
    params.language = options.language.c_str();
    params.initial_prompt = options.prompt ? options.prompt->c_str() : nullptr;
    params.abort_callback = [](void* data) { return static_cast<const CancelToken*>(data)->isCancelled(); };
    params.abort_callback_user_data = const_cast<CancelToken*>(&cancel);

    const bool timings = verboseLogging();
    if (timings) whisper_reset_timings(context);
    int status = whisper_full(context, params, samples.data(), static_cast<int>(samples.size()));
    if (timings) whisper_print_timings(context);
    if (cancel.isCancelled()) throw Cancelled();
    if (status != 0) throw std::runtime_error("전사 실패 (" + std::to_string(status) + ")");
    return collectSegments(options.language);
}

std::vector<Segment> WhisperEngine::collectSegments(const std::string& language) const {
    const whisper_token eot = whisper_token_eot(context_);
    std::vector<Segment> segments;
    const int count = whisper_full_n_segments(context_);
    for (int i = 0; i < count; ++i) {
        std::vector<TokenPiece> pieces;
        float logprobSum = 0;
        int logprobCount = 0;
        const int tokens = whisper_full_n_tokens(context_, i);
        for (int t = 0; t < tokens; ++t) {
            whisper_token_data data = whisper_full_get_token_data(context_, i, t);
            if (data.id >= eot) continue;
            logprobSum += data.plog;
            ++logprobCount;
            pieces.push_back(TokenPiece{whisper_full_get_token_text(context_, i, t), data.t0 / 100.0, data.t1 / 100.0, data.p});
        }
        Segment segment;
        segment.id = makeUUID();
        segment.start = whisper_full_get_segment_t0(context_, i) / 100.0;
        segment.end = whisper_full_get_segment_t1(context_, i) / 100.0;
        segment.text = whisper_full_get_segment_text(context_, i);
        const auto first = segment.text.find_first_not_of(" \t");
        const auto last = segment.text.find_last_not_of(" \t");
        segment.text = first == std::string::npos ? "" : segment.text.substr(first, last - first + 1);
        segment.language = language;
        auto words = assembleWords(pieces);
        if (!words.empty()) segment.words = words;
        if (logprobCount > 0) segment.avgLogprob = logprobSum / static_cast<float>(logprobCount);
        segment.noSpeechProb = whisper_full_get_segment_no_speech_prob(context_, i);
        segment.compressionRatio = compressionRatio(segment.text);
        segments.push_back(segment);
    }
    return segments;
}

VoiceActivityDetector::VoiceActivityDetector(const fs::path& model) {
    configureLogging();
    whisper_vad_context_params params = whisper_vad_default_context_params();
    params.use_gpu = false;
    params.n_threads = 2;
    context_ = whisper_vad_init_from_file_with_params(model.string().c_str(), params);
    if (!context_) throw std::runtime_error("VAD 모델을 불러오지 못했습니다: " + model.filename().string());
}

VoiceActivityDetector::~VoiceActivityDetector() {
    if (context_) whisper_vad_free(context_);
}

void VoiceActivityDetector::reset() {
    whisper_vad_reset_state(context_);
}

std::vector<float> VoiceActivityDetector::probabilities(const std::vector<float>& samples) {
    if (!whisper_vad_detect_speech_no_reset(context_, samples.data(), static_cast<int>(samples.size()))) {
        throw std::runtime_error("VAD 처리 실패");
    }
    const int count = whisper_vad_n_probs(context_);
    if (count != static_cast<int>((samples.size() + frameSize - 1) / frameSize)) throw std::runtime_error("VAD 프레임 수 불일치");
    const float* values = whisper_vad_probs(context_);
    return std::vector<float>(values, values + count);
}

SpeechMapBuilder::SpeechMapBuilder(VoiceActivityDetector& vad) : vad_(vad) {
    vad_.reset();
}

void SpeechMapBuilder::append(const std::vector<float>& samples) {
    if (samples.empty()) return;
    auto probabilities = vad_.probabilities(samples);
    probabilities_.insert(probabilities_.end(), probabilities.begin(), probabilities.end());
    const size_t frame = VoiceActivityDetector::frameSize;
    for (size_t offset = 0; offset < samples.size(); offset += frame) {
        const size_t end = std::min(offset + frame, samples.size());
        double sum = 0;
        for (size_t i = offset; i < end; ++i) sum += static_cast<double>(samples[i]) * samples[i];
        energies_.push_back(static_cast<float>(std::sqrt(sum / static_cast<double>(end - offset))));
    }
}

SpeechMap SpeechMapBuilder::finish(double duration) const {
    return SpeechMap{static_cast<double>(VoiceActivityDetector::frameSize) / kSampleRate, probabilities_, energies_, duration};
}

}
