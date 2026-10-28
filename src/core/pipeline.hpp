#ifndef SILLOK_CORE_PIPELINE_HPP_
#define SILLOK_CORE_PIPELINE_HPP_

#include <functional>
#include <string>
#include <vector>

#include "core/audio_buffer.hpp"
#include "core/cancel.hpp"
#include "core/language.hpp"
#include "core/model_provider.hpp"
#include "core/postprocess.hpp"
#include "core/segmenter.hpp"
#include "core/speakers.hpp"
#include "core/transcript.hpp"

namespace sillok {

enum class Stage { kAnalyze, kTranscribe, kDiarize };

// Progress of `stage` as a fraction in [0, 1].
using ProgressCallback = std::function<void(Stage stage, double fraction)>;

struct PipelineOptions {
  // "auto" or one of `candidates` (ISO 639-1).
  std::string language = "auto";
  std::vector<std::string> candidates = {"ko", "en"};
  std::vector<std::string> glossary;
  bool diarize = false;

  SegmenterOptions segmenter;
  LanguageOptions language_detection;
  PostprocessOptions postprocess;
  SpeakerOptions speakers;
  // A window whose mean token log-probability falls below this is decoded
  // again in the runner-up language, when there is one.
  double retry_below = -0.5;
  // Text of windows below this is not used as context for the next window.
  double min_context_quality = -0.8;
};

// Transcribes `audio` (finished). Models are opened one stage at a time and
// freed before the next stage, so the transcription and diarization models
// are never in memory together. Throws Cancelled once `stop` is requested.
Transcript RunPipeline(const AudioBuffer& audio, const PipelineOptions& options,
                       ModelProvider& models, const ProgressCallback& progress,
                       StopToken stop);

}  // namespace sillok

#endif  // SILLOK_CORE_PIPELINE_HPP_
