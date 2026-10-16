#pragma once

#include <utility>
#include <vector>

#include "types.hpp"

namespace sillok {

struct ChunkPlanner {
    float speechThreshold = 0.5f;
    float silenceThreshold = 0.35f;
    double minSilence = 0.5;
    double speechPad = 0.2;
    double maxChunk = 30;
    double forcedCutSearchStart = 25;
    double overlap = 0.5;

    std::vector<AudioChunk> plan(const SpeechMap& map) const;
    std::vector<std::pair<double, double>> speechRegions(const SpeechMap& map) const;
    double quietestTime(const SpeechMap& map, double start, double end) const;
};

void to_json(nlohmann::json& j, const ChunkPlanner& value);
void from_json(const nlohmann::json& j, ChunkPlanner& value);

}
