#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "types.hpp"

struct whisper_context;
struct whisper_vad_context;

namespace sillok {

std::string coreMLEncoderName(const std::string& ggmlFileName);
std::optional<float> compressionRatio(const std::string& text);

struct TokenPiece {
    std::string bytes;
    double start = 0;
    double end = 0;
    float probability = 0;
};

std::vector<WordTimestamp> assembleWords(const std::vector<TokenPiece>& tokens);

class WhisperEngine final : public Engine {
public:
    WhisperEngine() = default;
    ~WhisperEngine() override;
    WhisperEngine(const WhisperEngine&) = delete;
    WhisperEngine& operator=(const WhisperEngine&) = delete;

    void load(const std::filesystem::path& ggml, const std::optional<std::filesystem::path>& coreMLEncoder, bool useGPU);
    void unload();
    bool isLoaded() const { return context_ != nullptr; }

    LanguageDetection detectLanguage(const std::vector<float>& samples, const std::optional<std::set<std::string>>& allowed) override;
    int tokenCount(const std::string& text) override;
    std::vector<Segment> transcribe(const std::vector<float>& samples, const ChunkTranscribeOptions& options, const CancelToken& cancel) override;

private:
    whisper_context* context_ = nullptr;
    std::filesystem::path staging_;
    int threads_ = 4;

    whisper_context* require() const;
    std::vector<Segment> collectSegments(const std::string& language) const;
};

class VoiceActivityDetector {
public:
    static constexpr int frameSize = 512;

    explicit VoiceActivityDetector(const std::filesystem::path& model);
    ~VoiceActivityDetector();
    VoiceActivityDetector(const VoiceActivityDetector&) = delete;
    VoiceActivityDetector& operator=(const VoiceActivityDetector&) = delete;

    void reset();
    std::vector<float> probabilities(const std::vector<float>& samples);

private:
    whisper_vad_context* context_ = nullptr;
};

class SpeechMapBuilder {
public:
    explicit SpeechMapBuilder(VoiceActivityDetector& vad);

    void append(const std::vector<float>& samples);
    SpeechMap finish(double duration) const;

private:
    VoiceActivityDetector& vad_;
    std::vector<float> probabilities_;
    std::vector<float> energies_;
};

void configureLogging();
std::string systemInfo();

}
