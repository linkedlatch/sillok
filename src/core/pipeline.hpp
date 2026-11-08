#ifndef SILLOK_CORE_PIPELINE_HPP_
#define SILLOK_CORE_PIPELINE_HPP_

#include <cstddef>
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

// How far a run's transcription got: the segments of its first `groups`
// groups of speech (speech split where it pauses for longer than
// SegmenterOptions::max_gap, each transcribed on its own). A run stopped part
// way continues from here with the result one run would have given.
struct Checkpoint {
  std::string key;       // the recording, options and model it belongs to
  std::string language;  // the language it was transcribed in
  std::size_t groups = 0;
  std::vector<Segment> segments;
};

// Where a run records its checkpoint as transcription goes.
class CheckpointSink {
 public:
  virtual ~CheckpointSink() = default;
  // Transcription starts from the first group; anything recorded before is
  // void.
  virtual void Start(const std::string& key, const std::string& language) = 0;
  // The next group is done, with its own segments (maybe none).
  virtual void AddGroup(std::span<const Segment> segments) = 0;
};

// Transcribes `audio` (finished). Models are opened one stage at a time and
// freed before the next stage, so the transcription and diarization models
// are never in memory together. Throws Cancelled once `stop` is requested.
// With `resume` of the same recording, options and model, transcription
// continues after its groups, and `on_segments` first receives their
// segments; otherwise it starts from the first group. `checkpoints` (may be
// null) records the progress either way.
Transcript RunPipeline(const AudioBuffer& audio, const PipelineOptions& options,
                       ModelProvider& models, const ProgressCallback& progress,
                       StopToken stop,
                       const SegmentsCallback& on_segments = {},
                       const Checkpoint* resume = nullptr,
                       CheckpointSink* checkpoints = nullptr);

}  // namespace sillok

#endif  // SILLOK_CORE_PIPELINE_HPP_
