#include "segments.hpp"

#include <algorithm>

namespace sillok {

std::string trimSpaces(const std::string& text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    const auto last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

std::string joinWords(const std::vector<WordTimestamp>& words) {
    std::string text;
    for (const auto& word : words) text += word.word;
    return trimSpaces(text);
}

Segment offset(Segment segment, double delta) {
    segment.start += delta;
    segment.end += delta;
    if (segment.words) {
        for (auto& word : *segment.words) {
            word.start += delta;
            word.end += delta;
        }
    }
    return segment;
}

Segment clamped(Segment segment, double lower, double upper) {
    segment.start = std::clamp(segment.start, lower, upper);
    segment.end = std::clamp(segment.end, lower, upper);
    if (segment.words) {
        for (auto& word : *segment.words) {
            word.start = std::clamp(word.start, lower, upper);
            word.end = std::clamp(word.end, lower, upper);
        }
    }
    return segment;
}

std::vector<Segment> trimToChunk(const std::vector<Segment>& segments, const AudioChunk& chunk) {
    std::vector<Segment> result;
    result.reserve(segments.size());
    const bool hasCut = chunk.cutBefore.has_value() || chunk.cutAfter.has_value();
    auto keeps = [&](double mid) {
        if (chunk.cutBefore && mid < *chunk.cutBefore) return false;
        if (chunk.cutAfter && mid >= *chunk.cutAfter) return false;
        return true;
    };

    for (const auto& original : segments) {
        Segment segment = clamped(original, chunk.start, chunk.end);
        if (!hasCut) {
            result.push_back(segment);
            continue;
        }
        if (!segment.words || segment.words->empty()) {
            if (keeps((segment.start + segment.end) / 2)) result.push_back(segment);
            continue;
        }
        std::vector<WordTimestamp> kept;
        for (const auto& word : *segment.words) {
            if (keeps((word.start + word.end) / 2)) kept.push_back(word);
        }
        if (kept.empty()) continue;
        if (kept.size() != segment.words->size()) {
            segment.start = kept.front().start;
            segment.end = kept.back().end;
            segment.text = joinWords(kept);
            segment.words = kept;
        }
        result.push_back(segment);
    }
    return result;
}

bool endsSentence(const std::string& word) {
    const std::string trimmed = trimSpaces(word);
    for (const char* mark : {".", "?", "!", "\xE3\x80\x82", "\xEF\xBC\x9F", "\xEF\xBC\x81"}) {
        const std::string suffix(mark);
        if (trimmed.size() >= suffix.size() && trimmed.compare(trimmed.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return true;
        }
    }
    return false;
}

std::vector<Segment> SegmentSplitter::split(const std::vector<Segment>& segments) const {
    std::vector<Segment> result;
    for (const auto& segment : segments) {
        auto parts = split(segment);
        result.insert(result.end(), parts.begin(), parts.end());
    }
    return result;
}

std::vector<Segment> SegmentSplitter::split(const Segment& segment) const {
    if (!segment.words || segment.words->size() <= 1 || segment.end - segment.start <= maxDuration) return {segment};
    const auto& words = *segment.words;

    std::vector<std::pair<size_t, size_t>> groups;
    size_t start = 0;
    size_t index = 0;
    while (index < words.size()) {
        double span = words[index].end - words[start].start;
        if (span > maxDuration && index > start) {
            size_t cut = bestCut(words, start, index - 1);
            groups.emplace_back(start, cut);
            start = cut + 1;
            continue;
        }
        if (span >= minDuration && endsSentence(words[index].word) && index + 1 < words.size()) {
            groups.emplace_back(start, index);
            start = index + 1;
        }
        ++index;
    }
    if (start < words.size()) groups.emplace_back(start, words.size() - 1);

    std::vector<Segment> parts;
    parts.reserve(groups.size());
    for (const auto& [first, last] : groups) {
        Segment part = segment;
        part.id = makeUUID();
        part.words = std::vector<WordTimestamp>(words.begin() + first, words.begin() + last + 1);
        part.start = words[first].start;
        part.end = words[last].end;
        part.text = joinWords(*part.words);
        parts.push_back(part);
    }
    return parts;
}

size_t SegmentSplitter::bestCut(const std::vector<WordTimestamp>& words, size_t start, size_t last) const {
    size_t earliest = start;
    for (size_t i = start; i <= last; ++i) {
        if (words[i].end - words[start].start >= minDuration) {
            earliest = i;
            break;
        }
    }
    if (earliest >= last) return last;
    for (size_t i = last + 1; i-- > earliest;) {
        if (endsSentence(words[i].word)) return i;
    }
    auto gap = [&](size_t i) { return i + 1 < words.size() ? words[i + 1].start - words[i].end : 0.0; };
    size_t best = earliest;
    for (size_t i = earliest + 1; i <= last; ++i) {
        if (gap(i) > gap(best)) best = i;
    }
    return best;
}

std::vector<Segment> SegmentFilter::apply(const std::vector<Segment>& segments) const {
    std::vector<Segment> kept;
    for (auto segment : segments) {
        if (isSilenceHallucination(segment)) continue;
        segment.isLowConfidence = isLowConfidence(segment);
        kept.push_back(segment);
    }
    return kept;
}

bool SegmentFilter::isSilenceHallucination(const Segment& segment) const {
    if (!segment.noSpeechProb || !segment.avgLogprob) return false;
    return *segment.noSpeechProb > noSpeechThreshold && *segment.avgLogprob < logprobThreshold;
}

bool SegmentFilter::isLowConfidence(const Segment& segment) const {
    if (segment.compressionRatio && *segment.compressionRatio > compressionRatioThreshold) return true;
    if (segment.avgLogprob && *segment.avgLogprob < logprobThreshold) return true;
    return false;
}

}
