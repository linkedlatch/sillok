#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sillok {

constexpr int kSampleRate = 16000;

struct WordTimestamp {
    std::string word;
    double start = 0;
    double end = 0;
    float probability = 0;
};

struct Segment {
    std::string id;
    double start = 0;
    double end = 0;
    std::string text;
    std::string language;
    std::optional<std::string> speaker;
    std::optional<std::vector<WordTimestamp>> words;
    std::optional<float> avgLogprob;
    std::optional<float> noSpeechProb;
    std::optional<float> compressionRatio;
    bool isLowConfidence = false;
};

struct AudioChunk {
    double start = 0;
    double end = 0;
    std::optional<double> cutBefore;
    std::optional<double> cutAfter;

    double duration() const { return end - start; }
};

struct SpeechMap {
    double frameDuration = 0;
    std::vector<float> probabilities;
    std::vector<float> energies;
    double duration = 0;
};

struct LanguageMode {
    enum class Kind { automatic, koreanEnglish, fixed };
    Kind kind = Kind::automatic;
    std::string code;

    std::optional<std::set<std::string>> allowedLanguages() const;
};

struct TranscriptionOptions {
    LanguageMode languageMode;
    std::vector<std::string> vocabulary;
    int beamSize = 5;
    int maxPromptTokens = 150;
    bool speakerDiarization = false;
};

struct TranscriptionState {
    int nextChunk = 0;
    std::optional<std::string> previousLanguage;
    std::optional<std::string> previousText;
};

struct LanguageDetection {
    std::string language;
    double confidence = 0;
};

struct ChunkTranscribeOptions {
    std::string language;
    std::optional<std::string> prompt;
    int beamSize = 5;
};

class CancelToken {
public:
    void cancel() { cancelled_.store(true); }
    bool isCancelled() const { return cancelled_.load(); }

private:
    std::atomic<bool> cancelled_{false};
};

struct Cancelled : std::exception {
    const char* what() const noexcept override { return "cancelled"; }
};

using AudioReader = std::function<std::vector<float>(double start, double end)>;

class Engine {
public:
    virtual ~Engine() = default;
    virtual LanguageDetection detectLanguage(const std::vector<float>& samples, const std::optional<std::set<std::string>>& allowed) = 0;
    virtual int tokenCount(const std::string& text) = 0;
    virtual std::vector<Segment> transcribe(const std::vector<float>& samples, const ChunkTranscribeOptions& options, const CancelToken& cancel) = 0;
};

std::string makeUUID();

inline std::filesystem::path utf8Path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

inline std::string utf8String(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

void to_json(nlohmann::json& j, const WordTimestamp& value);
void from_json(const nlohmann::json& j, WordTimestamp& value);
void to_json(nlohmann::json& j, const Segment& value);
void from_json(const nlohmann::json& j, Segment& value);
void to_json(nlohmann::json& j, const AudioChunk& value);
void from_json(const nlohmann::json& j, AudioChunk& value);
void to_json(nlohmann::json& j, const LanguageMode& value);
void from_json(const nlohmann::json& j, LanguageMode& value);
void to_json(nlohmann::json& j, const TranscriptionOptions& value);
void from_json(const nlohmann::json& j, TranscriptionOptions& value);
void to_json(nlohmann::json& j, const TranscriptionState& value);
void from_json(const nlohmann::json& j, TranscriptionState& value);
void to_json(nlohmann::json& j, const LanguageDetection& value);
void to_json(nlohmann::json& j, const ChunkTranscribeOptions& value);
void from_json(const nlohmann::json& j, ChunkTranscribeOptions& value);

}
