#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

#include "types.hpp"

namespace nemo_speech::asr {
class Diarizer;
class DiarizationStream;
}

namespace sillok {

struct SpeakerTurn {
    double start = 0;
    double end = 0;
    int speaker = 0;
};

struct SpeakerAssigner {
    double minRunDuration = 1.0;

    std::vector<Segment> assign(const std::vector<Segment>& segments, const std::vector<SpeakerTurn>& turns) const;
};

class SpeakerDiarizer {
public:
    static constexpr double windowSeconds = 60;

    SpeakerDiarizer(const std::filesystem::path& model, bool useGPU);
    ~SpeakerDiarizer();
    SpeakerDiarizer(const SpeakerDiarizer&) = delete;
    SpeakerDiarizer& operator=(const SpeakerDiarizer&) = delete;

    std::vector<SpeakerTurn> diarize(const AudioReader& read, double duration, const CancelToken& cancel);
    void release();

private:
    std::mutex mutex_;
    std::shared_ptr<nemo_speech::asr::Diarizer> diarizer_;
    std::unique_ptr<nemo_speech::asr::DiarizationStream> stream_;
};

}
