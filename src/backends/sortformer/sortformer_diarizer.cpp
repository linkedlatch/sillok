#include "backends/sortformer/sortformer_diarizer.hpp"

#include <cmath>

#include <diarizer.h>

#include "core/files.hpp"
#include "core/model_provider.hpp"

namespace sillok {
namespace {

namespace nemo = nemo_speech::asr;

SampleIndex ToSampleIndex(double seconds) {
  return static_cast<SampleIndex>(std::llround(seconds * kSampleRate));
}

class SortformerDiarizer final : public Diarizer {
 public:
  explicit SortformerDiarizer(const SortformerConfig& config)
      : info_{.name = config.name,
              .device = config.device.name,
              .fell_back_to_cpu = config.device.fell_back_to_cpu} {
    try {
      diarizer_ = nemo::Diarizer::load(config.device.gpu_index,
                                       PathToUtf8(config.model),
                                       nemo::DiarGeometry::preset(config.preset));
      stream_ = diarizer_->streaming_diarize();
    } catch (const std::exception& error) {
      throw ModelError(std::string("cannot load the diarization model: ") +
                       error.what());
    }
  }

  ModelInfo Info() const override { return info_; }

  void Push(std::span<const float> pcm) override {
    try {
      stream_->push(pcm.data(), pcm.size(), kSampleRate);
    } catch (const std::exception& error) {
      throw ModelError(std::string("diarization failed: ") + error.what());
    }
  }

  std::vector<SpeakerTurn> Finish() override {
    std::vector<nemo::DiarSegment> segments;
    try {
      stream_->finish();
      segments = stream_->segments();
    } catch (const std::exception& error) {
      throw ModelError(std::string("diarization failed: ") + error.what());
    }
    std::vector<SpeakerTurn> turns;
    turns.reserve(segments.size());
    for (const nemo::DiarSegment& segment : segments) {
      turns.push_back({.range = {ToSampleIndex(segment.t0), ToSampleIndex(segment.t1)},
                       .speaker = segment.speaker});
    }
    return turns;
  }

 private:
  ModelInfo info_;
  std::shared_ptr<nemo::Diarizer> diarizer_;
  std::unique_ptr<nemo::DiarizationStream> stream_;
};

}  // namespace

std::unique_ptr<Diarizer> OpenSortformer(const SortformerConfig& config) {
  return std::make_unique<SortformerDiarizer>(config);
}

}  // namespace sillok
