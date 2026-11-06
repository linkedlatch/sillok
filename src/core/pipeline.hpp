#ifndef SILLOK_CORE_PIPELINE_HPP_
#define SILLOK_CORE_PIPELINE_HPP_

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "core/audio_buffer.hpp"
#include "core/cancel.hpp"
#include "core/model_provider.hpp"
#include "core/postprocess.hpp"
#include "core/segmenter.hpp"
#include "core/speakers.hpp"
#include "core/transcript.hpp"

namespace sillok {

enum class Stage { kAnalyze, kTranscribe, kDiarize };

// Progress of `stage` as a fraction in [0, 1].
using ProgressCallback = std::function<void(Stage stage, double fraction)>;

// Transcribed segments, in order, as the transcription stage decodes them.
// They preview the transcript, which then adds speakers and splits long
// segments.
using SegmentsCallback = std::function<void(std::span<const Segment> segments)>;

struct PipelineOptions {
  // "auto", detected once from the first speech, or one of `candidates`
  // (ISO 639-1).
  std::string language = "auto";
  std::vector<std::string> candidates = {"ko", "en"};
  std::vector<std::string> glossary;
  bool diarize = false;

  SegmenterOptions segmenter;
  PostprocessOptions postprocess;
  SpeakerOptions speakers;
};

// Transcribes `audio` (finished). Models are opened one stage at a time and
// freed before the next stage, so the transcription and diarization models
// are never in memory together. Throws Cancelled once `stop` is requested.
Transcript RunPipeline(const AudioBuffer& audio, const PipelineOptions& options,
                       ModelProvider& models, const ProgressCallback& progress,
                       StopToken stop,
                       const SegmentsCallback& on_segments = {});

}  // namespace sillok

#endif  // SILLOK_CORE_PIPELINE_HPP_
