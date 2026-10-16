#include "types.hpp"

#include <cstdio>
#include <random>

namespace sillok {

using nlohmann::json;

std::optional<std::set<std::string>> LanguageMode::allowedLanguages() const {
    switch (kind) {
    case Kind::automatic: return std::nullopt;
    case Kind::koreanEnglish: return std::set<std::string>{"ko", "en"};
    case Kind::fixed: return std::set<std::string>{code};
    }
    return std::nullopt;
}

std::string makeUUID() {
    thread_local std::mt19937_64 generator{std::random_device{}()};
    uint64_t high = generator();
    uint64_t low = generator();
    high = (high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    low = (low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    char buffer[37];
    std::snprintf(buffer, sizeof buffer, "%08X-%04X-%04X-%04X-%012llX",
                  static_cast<unsigned>(high >> 32), static_cast<unsigned>((high >> 16) & 0xFFFF),
                  static_cast<unsigned>(high & 0xFFFF), static_cast<unsigned>(low >> 48),
                  static_cast<unsigned long long>(low & 0xFFFFFFFFFFFFULL));
    return buffer;
}

template <typename T>
static void putOptional(json& j, const char* key, const std::optional<T>& value) {
    if (value) j[key] = *value;
}

template <typename T>
static void getOptional(const json& j, const char* key, std::optional<T>& value) {
    auto it = j.find(key);
    value = (it != j.end() && !it->is_null()) ? std::optional<T>(it->get<T>()) : std::nullopt;
}

void to_json(json& j, const WordTimestamp& value) {
    j = json{{"word", value.word}, {"start", value.start}, {"end", value.end}, {"probability", value.probability}};
}

void from_json(const json& j, WordTimestamp& value) {
    j.at("word").get_to(value.word);
    j.at("start").get_to(value.start);
    j.at("end").get_to(value.end);
    j.at("probability").get_to(value.probability);
}

void to_json(json& j, const Segment& value) {
    j = json{{"id", value.id}, {"start", value.start}, {"end", value.end}, {"text", value.text},
             {"language", value.language}, {"isLowConfidence", value.isLowConfidence}};
    putOptional(j, "speaker", value.speaker);
    putOptional(j, "words", value.words);
    putOptional(j, "avgLogprob", value.avgLogprob);
    putOptional(j, "noSpeechProb", value.noSpeechProb);
    putOptional(j, "compressionRatio", value.compressionRatio);
}

void from_json(const json& j, Segment& value) {
    value.id = j.value("id", makeUUID());
    j.at("start").get_to(value.start);
    j.at("end").get_to(value.end);
    j.at("text").get_to(value.text);
    j.at("language").get_to(value.language);
    value.isLowConfidence = j.value("isLowConfidence", false);
    getOptional(j, "speaker", value.speaker);
    getOptional(j, "words", value.words);
    getOptional(j, "avgLogprob", value.avgLogprob);
    getOptional(j, "noSpeechProb", value.noSpeechProb);
    getOptional(j, "compressionRatio", value.compressionRatio);
}

void to_json(json& j, const AudioChunk& value) {
    j = json{{"start", value.start}, {"end", value.end}};
    putOptional(j, "cutBefore", value.cutBefore);
    putOptional(j, "cutAfter", value.cutAfter);
}

void from_json(const json& j, AudioChunk& value) {
    j.at("start").get_to(value.start);
    j.at("end").get_to(value.end);
    getOptional(j, "cutBefore", value.cutBefore);
    getOptional(j, "cutAfter", value.cutAfter);
}

void to_json(json& j, const LanguageMode& value) {
    switch (value.kind) {
    case LanguageMode::Kind::automatic: j = json{{"auto", json::object()}}; break;
    case LanguageMode::Kind::koreanEnglish: j = json{{"koreanEnglish", json::object()}}; break;
    case LanguageMode::Kind::fixed: j = json{{"fixed", json{{"_0", value.code}}}}; break;
    }
}

void from_json(const json& j, LanguageMode& value) {
    if (j.contains("koreanEnglish")) {
        value = LanguageMode{LanguageMode::Kind::koreanEnglish, ""};
    } else if (j.contains("fixed")) {
        value = LanguageMode{LanguageMode::Kind::fixed, j.at("fixed").at("_0").get<std::string>()};
    } else {
        value = LanguageMode{};
    }
}

void to_json(json& j, const TranscriptionOptions& value) {
    j = json{{"languageMode", value.languageMode}, {"vocabulary", value.vocabulary},
             {"beamSize", value.beamSize}, {"maxPromptTokens", value.maxPromptTokens}};
}

void from_json(const json& j, TranscriptionOptions& value) {
    value.languageMode = j.value("languageMode", LanguageMode{});
    value.vocabulary = j.value("vocabulary", std::vector<std::string>{});
    value.beamSize = j.value("beamSize", 5);
    value.maxPromptTokens = j.value("maxPromptTokens", 150);
}

void to_json(json& j, const TranscriptionState& value) {
    j = json{{"nextChunk", value.nextChunk}};
    putOptional(j, "previousLanguage", value.previousLanguage);
    putOptional(j, "previousText", value.previousText);
}

void from_json(const json& j, TranscriptionState& value) {
    value.nextChunk = j.value("nextChunk", 0);
    getOptional(j, "previousLanguage", value.previousLanguage);
    getOptional(j, "previousText", value.previousText);
}

void to_json(json& j, const LanguageDetection& value) {
    j = json{{"language", value.language}, {"confidence", value.confidence}};
}

void to_json(json& j, const ChunkTranscribeOptions& value) {
    j = json{{"language", value.language}, {"beamSize", value.beamSize}};
    putOptional(j, "prompt", value.prompt);
}

void from_json(const json& j, ChunkTranscribeOptions& value) {
    j.at("language").get_to(value.language);
    value.beamSize = j.value("beamSize", 5);
    getOptional(j, "prompt", value.prompt);
}

}
