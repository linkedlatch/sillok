#pragma once

#include <vector>

#include "segments.hpp"
#include "types.hpp"

namespace sillok {

struct StepResult {
    TranscriptionState state;
    std::vector<Segment> segments;
};

struct ChunkTranscriber {
    TranscriptionOptions options;
    SegmentFilter filter;
    SegmentSplitter splitter;

    StepResult step(const TranscriptionState& state, const std::vector<AudioChunk>& chunks, const AudioReader& read,
                    Engine& engine, const CancelToken& cancel) const;
};

std::string exportText(const std::vector<Segment>& segments);
std::string exportSubRip(const std::vector<Segment>& segments);
std::string subRipTimestamp(double seconds);

}
