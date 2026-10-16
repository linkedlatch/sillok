#include "chunking.hpp"

#include <algorithm>
#include <cmath>

namespace sillok {

std::vector<AudioChunk> ChunkPlanner::plan(const SpeechMap& map) const {
    std::vector<AudioChunk> chunks;
    std::optional<AudioChunk> current;

    for (const auto& [regionStart, regionEnd] : speechRegions(map)) {
        if (current && regionEnd - current->start <= maxChunk) {
            current->end = regionEnd;
            continue;
        }
        if (current) chunks.push_back(*current);

        AudioChunk chunk{regionStart, regionEnd, std::nullopt, std::nullopt};
        while (chunk.end - chunk.start > maxChunk) {
            double cut = quietestTime(map, chunk.start + forcedCutSearchStart, chunk.start + maxChunk - overlap);
            chunks.push_back(AudioChunk{chunk.start, cut + overlap, chunk.cutBefore, cut});
            chunk = AudioChunk{cut - overlap, chunk.end, cut, std::nullopt};
        }
        current = chunk;
    }
    if (current) chunks.push_back(*current);
    return chunks;
}

std::vector<std::pair<double, double>> ChunkPlanner::speechRegions(const SpeechMap& map) const {
    std::vector<std::pair<int, int>> raw;
    std::optional<int> speechStart;
    const int count = static_cast<int>(map.probabilities.size());
    for (int index = 0; index < count; ++index) {
        float probability = map.probabilities[index];
        if (speechStart) {
            if (probability < silenceThreshold) {
                raw.emplace_back(*speechStart, index - 1);
                speechStart.reset();
            }
        } else if (probability >= speechThreshold) {
            speechStart = index;
        }
    }
    if (speechStart) raw.emplace_back(*speechStart, count - 1);

    const int minSilenceFrames = static_cast<int>(std::ceil(minSilence / map.frameDuration));
    std::vector<std::pair<int, int>> merged;
    for (const auto& range : raw) {
        if (!merged.empty() && range.first - merged.back().second - 1 < minSilenceFrames) {
            merged.back().second = range.second;
        } else {
            merged.push_back(range);
        }
    }

    std::vector<std::pair<double, double>> regions;
    regions.reserve(merged.size());
    for (const auto& [first, last] : merged) {
        double start = first * map.frameDuration - speechPad;
        double end = (last + 1) * map.frameDuration + speechPad;
        regions.emplace_back(std::max(start, 0.0), std::min(end, map.duration));
    }
    return regions;
}

double ChunkPlanner::quietestTime(const SpeechMap& map, double start, double end) const {
    const int count = static_cast<int>(map.energies.size());
    int first = std::max(static_cast<int>(std::ceil(start / map.frameDuration)), 0);
    int last = std::min(static_cast<int>(std::floor(end / map.frameDuration)) - 1, count - 1);
    if (first > last) return end;
    int best = first;
    for (int index = first + 1; index <= last; ++index) {
        if (map.energies[index] < map.energies[best]) best = index;
    }
    return std::clamp((best + 0.5) * map.frameDuration, start, end);
}

void to_json(nlohmann::json& j, const ChunkPlanner& value) {
    j = nlohmann::json{{"speechThreshold", value.speechThreshold}, {"silenceThreshold", value.silenceThreshold},
                       {"minSilence", value.minSilence}, {"speechPad", value.speechPad}, {"maxChunk", value.maxChunk},
                       {"forcedCutSearchStart", value.forcedCutSearchStart}, {"overlap", value.overlap}};
}

void from_json(const nlohmann::json& j, ChunkPlanner& value) {
    ChunkPlanner defaults;
    value.speechThreshold = j.value("speechThreshold", defaults.speechThreshold);
    value.silenceThreshold = j.value("silenceThreshold", defaults.silenceThreshold);
    value.minSilence = j.value("minSilence", defaults.minSilence);
    value.speechPad = j.value("speechPad", defaults.speechPad);
    value.maxChunk = j.value("maxChunk", defaults.maxChunk);
    value.forcedCutSearchStart = j.value("forcedCutSearchStart", defaults.forcedCutSearchStart);
    value.overlap = j.value("overlap", defaults.overlap);
}

}
