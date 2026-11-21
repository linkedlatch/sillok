#ifndef SILLOK_CORE_PIPELINE_HPP_
#define SILLOK_CORE_PIPELINE_HPP_

#include <cstddef>
#include <functional>
#include <map>
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

enum class Stage { kAnalyze, kTranscribe, kAlign, kDiarize };

// Progress of `stage` as a fraction in [0, 1].
using ProgressCallback = std::function<void(Stage stage, double fraction)>;

// Segments of chunk `chunk` (in ChunkSpeech's order) for the app to show
// while the run goes on: as the transcription stage decodes them, or, for a
// transcriber without word times, first as a draft and then as the alignment
// stage times them. A draft is one segment spanning the chunk with the text
// it heard and no words; the chunk's segments from the alignment stage, all
// in one preview, replace it. Previews come in chunk order within a stage.
// They preview the transcript, which then adds speakers and splits long
// segments.
struct Preview {
  std::size_t chunk = 0;
  bool draft = false;
  std::span<const Segment> segments;
};
using SegmentsCallback = std::function<void(const Preview& preview)>;

struct PipelineOptions {
  // "auto", told by the model for each chunk of speech (ChunkSpeech), or one of
  // ModelProvider::Languages() (ISO 639-1).
  std::string language = "auto";
  std::vector<std::string> glossary;
  bool diarize = false;

  SegmenterOptions segmenter;
  PostprocessOptions postprocess;
};

// What the transcription or alignment stage made of one chunk.
struct ChunkResult {
  std::vector<Segment> segments;  // timed in the recording, without speakers
  // What a transcriber without word times heard (then no segments), for the
  // aligner; empty otherwise.
  std::string text;
  std::string language;
};

// What an earlier run of the same recording, options and models finished,
// chunk by chunk, for a run stopped part way to go on from there.
// Transcription and alignment are kept; detecting speech, diarization and
// splitting segments are quick and run again.
struct Checkpoint {
  std::string key;  // the recording, options and models it belongs to
  // The chunks' limit, set by the transcriber, so the same chunks follow
  // without loading it.
  Milliseconds max_chunk{0};
  std::map<std::size_t, ChunkResult> transcribed;
  std::map<std::size_t, ChunkResult> aligned;
  // The models that ran those stages, as in Transcript::models.
  std::map<std::string, ModelInfo> models;
};

// Where a run records its checkpoint as it goes.
class CheckpointSink {
 public:
  virtual ~CheckpointSink() = default;
  // The run starts from the first chunk; anything recorded before is void.
  virtual void Start(const std::string& key, Milliseconds max_chunk) = 0;
  // `stage` (kTranscribe or kAlign) is done with `chunk`.
  virtual void AddChunk(Stage stage, std::size_t chunk, const ChunkResult& result) = 0;
  // A stage's model, named as in Transcript::models.
  virtual void AddModel(const std::string& stage, const ModelInfo& info) = 0;
};

// Transcribes `audio` (finished). Models are opened one stage at a time and
// freed before the next stage, so the transcription, alignment and
// diarization models are never in memory together. Throws Cancelled once `stop` is requested.
// Throws std::invalid_argument for a language the model does not know.
// With `resume` of the same recording, options and models, the run skips the
// chunks it holds, previewing them first, and gives the transcript one run
// would have given; otherwise it starts anew. `checkpoints` (may be null)
// records the progress either way.
Transcript RunPipeline(const AudioBuffer& audio, const PipelineOptions& options,
                       ModelProvider& models, const ProgressCallback& progress,
                       StopToken stop,
                       const SegmentsCallback& on_segments = {},
                       const Checkpoint* resume = nullptr,
                       CheckpointSink* checkpoints = nullptr);

}  // namespace sillok

#endif  // SILLOK_CORE_PIPELINE_HPP_
