#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "types.hpp"

namespace sillok::testing {

inline const std::string testModel = "ggml-large-v3-turbo-q5_0.bin";
inline const std::string vadModel = "ggml-silero-v6.2.0.bin";

inline std::filesystem::path repoRoot() {
    return std::filesystem::path(SILLOK_REPO_ROOT);
}

inline std::filesystem::path modelPath(const std::string& name) {
    return repoRoot() / "models" / name;
}

inline std::filesystem::path fixturePath(const std::string& name) {
    return repoRoot() / "build" / "fixtures" / name;
}

inline std::vector<float> readWav(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    size_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        std::string id(bytes.data() + offset, 4);
        uint32_t size = 0;
        std::memcpy(&size, bytes.data() + offset + 4, 4);
        if (id == "data") {
            std::vector<float> samples(size / 2);
            for (size_t i = 0; i < samples.size(); ++i) {
                int16_t value = 0;
                std::memcpy(&value, bytes.data() + offset + 8 + i * 2, 2);
                samples[i] = value / 32768.0f;
            }
            return samples;
        }
        offset += 8 + size + (size & 1);
    }
    return {};
}

inline AudioReader silentReader() {
    return [](double start, double end) {
        return std::vector<float>(static_cast<size_t>(std::max(0.0, end - start) * kSampleRate), 0.0f);
    };
}

inline AudioReader arrayReader(const std::vector<float>& samples) {
    return [&samples](double start, double end) {
        size_t lower = std::min(samples.size(), static_cast<size_t>(std::max(0.0, start) * kSampleRate + 0.5));
        size_t upper = std::min(samples.size(), static_cast<size_t>(std::max(0.0, end) * kSampleRate + 0.5));
        return lower < upper ? std::vector<float>(samples.begin() + lower, samples.begin() + upper) : std::vector<float>{};
    };
}

class FakeEngine final : public Engine {
public:
    std::string detectedLanguage = "en";
    double detectionConfidence = 0.9;
    std::deque<LanguageDetection> scriptedDetections;
    std::deque<std::vector<Segment>> nextSegments;
    std::vector<std::pair<size_t, std::optional<std::set<std::string>>>> detections;
    std::vector<ChunkTranscribeOptions> transcriptions;

    LanguageDetection detectLanguage(const std::vector<float>& samples, const std::optional<std::set<std::string>>& allowed) override {
        detections.emplace_back(samples.size(), allowed);
        if (!scriptedDetections.empty()) {
            auto detection = scriptedDetections.front();
            scriptedDetections.pop_front();
            return detection;
        }
        return LanguageDetection{detectedLanguage, detectionConfidence};
    }

    int tokenCount(const std::string& text) override {
        int count = 0;
        bool inWord = false;
        for (char c : text) {
            if (c == ' ') {
                inWord = false;
            } else if (!inWord) {
                inWord = true;
                ++count;
            }
        }
        return count;
    }

    std::vector<Segment> transcribe(const std::vector<float>&, const ChunkTranscribeOptions& options, const CancelToken&) override {
        transcriptions.push_back(options);
        if (nextSegments.empty()) return {};
        auto segments = nextSegments.front();
        nextSegments.pop_front();
        return segments;
    }
};

inline WordTimestamp word(const std::string& text, double start, double end, float probability = 0.9f) {
    return WordTimestamp{text, start, end, probability};
}

inline Segment segmentOf(const std::vector<WordTimestamp>& words, const std::string& language = "en") {
    Segment segment;
    segment.id = makeUUID();
    segment.start = words.front().start;
    segment.end = words.back().end;
    std::string text;
    for (const auto& w : words) text += w.word;
    segment.text = text.substr(text.find_first_not_of(' '));
    segment.language = language;
    segment.words = words;
    return segment;
}

inline Segment plainSegment(const std::string& text, double start, double end, std::optional<float> logprob = -0.2f) {
    Segment segment;
    segment.id = makeUUID();
    segment.start = start;
    segment.end = end;
    segment.text = text;
    segment.avgLogprob = logprob;
    segment.noSpeechProb = 0.01f;
    segment.compressionRatio = 1.2f;
    return segment;
}

struct SeededGenerator {
    uint64_t state;
    uint64_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state >> 11;
    }
    double uniform(double lower, double upper) { return lower + (upper - lower) * static_cast<double>(next() % 1000000) / 1000000.0; }
    int integer(int lower, int upper) { return lower + static_cast<int>(next() % static_cast<uint64_t>(upper - lower + 1)); }
};

}
