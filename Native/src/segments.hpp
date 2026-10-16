#pragma once

#include <string>
#include <vector>

#include "types.hpp"

namespace sillok {

std::string trimSpaces(const std::string& text);
std::string joinWords(const std::vector<WordTimestamp>& words);

Segment offset(Segment segment, double delta);
Segment clamped(Segment segment, double lower, double upper);
std::vector<Segment> trimToChunk(const std::vector<Segment>& segments, const AudioChunk& chunk);

struct SegmentSplitter {
    double maxDuration = 7;
    double minDuration = 1.5;

    std::vector<Segment> split(const std::vector<Segment>& segments) const;
    std::vector<Segment> split(const Segment& segment) const;

private:
    size_t bestCut(const std::vector<WordTimestamp>& words, size_t start, size_t last) const;
};

struct SegmentFilter {
    float noSpeechThreshold = 0.6f;
    float logprobThreshold = -1.0f;
    float compressionRatioThreshold = 2.4f;

    std::vector<Segment> apply(const std::vector<Segment>& segments) const;
    bool isSilenceHallucination(const Segment& segment) const;
    bool isLowConfidence(const Segment& segment) const;
};

bool endsSentence(const std::string& word);

}
