#include "core/pipeline.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "core/aligner.hpp"
#include "core/audio.hpp"
#include "core/cancel.hpp"
#include "core/diarizer.hpp"
#include "core/postprocess.hpp"
#include "core/sha256.hpp"
#include "core/transcriber.hpp"
#include "core/words.hpp"

namespace sillok {
namespace {

constexpr SampleIndex kDiarizationChunk = SampleIndex{kSampleRate} * 10;

// Where the recording has speech, and how likely each frame is speech.
struct Speech {
  std::vector<SampleRange> regions;
  std::vector<float> probabilities;
  int frame_samples = 1;
};

void Add(Sha256& hash, std::string_view text) {
  hash.Update(std::span(reinterpret_cast<const unsigned char*>(text.data()), text.size()));
  hash.Update(std::span(reinterpret_cast<const unsigned char*>("\n"), 1));
}

// Identifies a recording, for the keys of what a run keeps.
std::string HashAudio(const AudioBuffer& audio) {
  Sha256 hash;
  constexpr SampleIndex kBlock = SampleIndex{kSampleRate} * 60;
  for (SampleIndex begin = 0; begin < audio.size(); begin += kBlock) {
    const std::vector<float> part = audio.Read({begin, begin + kBlock});
    hash.Update(std::span(reinterpret_cast<const unsigned char*>(part.data()),
                          part.size() * sizeof(float)));
  }
  return hash.HexDigest();
}

// Identifies the speech `detector` detects in the recording hashed as
// `recording`. Bump the first line when the same input gives other
// probabilities.
std::string SpeechKey(const std::string& recording, const VoiceDetector& detector) {
  Sha256 hash;
  Add(hash, "sillok speech 1");
  Add(hash, detector.Info().name);
  Add(hash, recording);
  return hash.HexDigest();
}

std::vector<float> DetectProbabilities(const AudioBuffer& audio, VoiceDetector& detector,
                                       const ProgressCallback& progress, const StopToken& stop) {
  std::vector<float> probabilities;
  // About 30 s per call, in whole frames so the detector state carries over.
  const SampleIndex frame = detector.FrameSamples();
  const SampleIndex chunk = std::max<SampleIndex>(1, SampleIndex{kSampleRate} * 30 / frame) * frame;
  for (SampleIndex begin = 0; begin < audio.size(); begin += chunk) {
    ThrowIfStopped(stop);
    const std::vector<float> part = detector.Detect(audio.Read({begin, begin + chunk}));
    probabilities.insert(probabilities.end(), part.begin(), part.end());
    progress(Stage::kAnalyze,
             static_cast<double>(std::min(begin + chunk, audio.size())) /
                 static_cast<double>(audio.size()));
  }
  return probabilities;
}

// `store` (may be null) keeps the probabilities of the recording hashed as
// `recording`.
Speech DetectSpeech(const AudioBuffer& audio, const SegmenterOptions& options,
                    ModelProvider& models, const ProgressCallback& progress,
                    const StopToken& stop, SpeechStore* store, const std::string& recording,
                    Transcript& transcript) {
  const auto detector = models.OpenVoiceDetector();
  Speech speech{.regions = {}, .probabilities = {}, .frame_samples = detector->FrameSamples()};
  const auto frames = static_cast<std::size_t>((audio.size() + speech.frame_samples - 1) /
                                               speech.frame_samples);
  const std::string key = store != nullptr ? SpeechKey(recording, *detector) : "";
  std::optional<DetectedSpeech> saved = store != nullptr ? store->Load() : std::nullopt;
  if (saved && saved->key == key && saved->probabilities.size() == frames) {
    speech.probabilities = std::move(saved->probabilities);
    progress(Stage::kAnalyze, 1.0);
  } else {
    speech.probabilities = DetectProbabilities(audio, *detector, progress, stop);
    if (store != nullptr) store->Save({.key = key, .probabilities = speech.probabilities});
  }
  transcript.models["voice_detection"] = detector->Info();
  speech.regions = FindSpeech(speech.probabilities, speech.frame_samples, audio.size(), options);
  return speech;
}

// Identifies what a checkpoint was made of: the recording (hashed as
// `recording`) and the options that shape its chunks and their
// transcription. The models are the caller's to tell apart
// (CheckpointSink). Bump the first line when the same input gives other
// chunks or other results.
std::string CheckpointKey(const std::string& recording, const PipelineOptions& options) {
  Sha256 hash;
  const auto add = [&](std::string_view text) { Add(hash, text); };
  add("sillok checkpoint 5");
  add(options.language);
  for (const std::string& term : options.glossary) add(term);
  add("");
  const SegmenterOptions& segmenter = options.segmenter;
  for (const float threshold : {segmenter.speech_threshold, segmenter.silence_threshold}) {
    add(std::to_string(threshold));
  }
  for (const Milliseconds time : {segmenter.min_speech, segmenter.min_silence, segmenter.before,
                                  segmenter.after, segmenter.max_chunk}) {
    add(std::to_string(time.count()));
  }
  add(recording);
  return hash.HexDigest();
}

// What a run goes on from and where it records how far it got.
struct Resumption {
  const Checkpoint* resume = nullptr;  // of this run's key; null to start anew
  CheckpointSink* sink = nullptr;      // may be null
  std::string key;

  const ChunkResult* Saved(Stage stage, std::size_t chunk) const {
    if (resume == nullptr) return nullptr;
    const auto& saved = stage == Stage::kTranscribe ? resume->transcribed : resume->aligned;
    const auto found = saved.find(chunk);
    return found == saved.end() ? nullptr : &found->second;
  }
  void Add(Stage stage, std::size_t chunk, const ChunkResult& result) const {
    if (sink != nullptr) sink->AddChunk(stage, chunk, result);
  }
  // The model that ran `stage` in the earlier run, for a stage that did not
  // load its own.
  void SavedModel(const std::string& stage, Transcript& transcript) const {
    if (resume == nullptr) return;
    const auto found = resume->models.find(stage);
    if (found != resume->models.end()) transcript.models[stage] = found->second;
  }
};

// A chunk of speech, joined, with its place in the recording.
std::vector<float> ReadJoined(const AudioBuffer& audio, std::span<const SampleRange> chunk,
                              const JoinedSpeech& joined) {
  std::vector<float> pcm(static_cast<std::size_t>(joined.size()), 0.0f);
  for (std::size_t i = 0; i < chunk.size(); ++i) {
    const std::vector<float> part = audio.Read(chunk[i]);
    std::copy(part.begin(), part.end(),
              pcm.begin() + static_cast<std::ptrdiff_t>(joined.Offset(i)));
  }
  return pcm;
}

// `words` of a chunk, timed within it, as a segment timed within the
// recording; none when no word is left.
std::optional<Segment> ToSegment(std::vector<Word> words, std::string language,
                                 const JoinedSpeech& joined) {
  words = CheckWords(std::move(words), ToMilliseconds(joined.size()));
  if (words.empty()) return std::nullopt;
  for (Word& word : words) {
    word.speaker = std::nullopt;
    std::tie(word.start, word.end) = joined.ToRecording(word.start, word.end);
  }
  return MakeSegment(std::move(words), std::move(language));
}

// What a transcriber heard in a chunk without word times, for the aligner.
struct Heard {
  std::size_t index = 0;  // of the chunk
  std::vector<SampleRange> chunk;
  std::string text;
  std::string language;
};

// The draft preview of what was heard: one segment over the chunk.
Segment Draft(const Heard& heard) {
  return Segment{.start = ToMilliseconds(heard.chunk.front().begin),
                 .end = ToMilliseconds(heard.chunk.back().end),
                 .text = heard.text,
                 .language = heard.language,
                 .speaker = std::nullopt,
                 .words = {}};
}

void Show(const SegmentsCallback& on_segments, std::size_t chunk, bool draft,
          std::span<const Segment> segments) {
  if (on_segments && !segments.empty()) on_segments({.chunk = chunk, .draft = draft, .segments = segments});
}

// The transcription stage: segments with word times, or, from a transcriber
// that leaves them to an aligner, what it heard in each chunk.
struct Transcribed {
  std::vector<Segment> segments;
  std::vector<Heard> heard;
};

Transcribed TranscribeSpeech(
    const AudioBuffer& audio, const Speech& speech,
    const PipelineOptions& options, ModelProvider& models,
    const ProgressCallback& progress, const SegmentsCallback& on_segments,
    const StopToken& stop, const Resumption& run, Transcript& transcript) {
  progress(Stage::kTranscribe,
           0.0);  // before the model loads, which takes a while
  ThrowIfStopped(stop);
  std::unique_ptr<Transcriber> transcriber;
  Milliseconds longest{0};
  const auto open = [&] {
    transcriber = models.OpenTranscriber();
    if (run.resume == nullptr) {
      longest = std::min(options.segmenter.max_chunk, transcriber->MaxAudio());
      if (run.sink != nullptr) run.sink->Start(run.key, longest);
    }
    transcriber->CheckGlossary(options.glossary, longest);
    if (run.sink != nullptr) run.sink->AddModel("transcription", transcriber->Info());
  };
  // A run going on takes the earlier run's chunks, without loading the model
  // when they are all done.
  if (run.resume != nullptr) {
    longest = run.resume->max_chunk;
  } else {
    open();
  }
  const std::vector<std::vector<SampleRange>> chunks =
      ChunkSpeech(speech.regions, speech.probabilities, speech.frame_samples, longest);
  // Each chunk's language is told from its own speech: a recording may change
  // language between chunks.
  const std::string language = options.language == "auto" ? "" : options.language;

  Transcribed result;
  SampleIndex total_samples = 0;
  SampleIndex done_samples = 0;
  for (const std::vector<SampleRange>& chunk : chunks) total_samples += JoinedSpeech(chunk).size();

  for (std::size_t index = 0; index < chunks.size(); ++index) {
    ThrowIfStopped(stop);
    const std::vector<SampleRange>& chunk = chunks[index];
    const JoinedSpeech joined(chunk);
    ChunkResult done;
    if (const ChunkResult* saved = run.Saved(Stage::kTranscribe, index)) {
      done = *saved;
      Show(on_segments, index, false, done.segments);
    } else {
      if (!transcriber) open();
      const std::vector<float> pcm = ReadJoined(audio, chunk, joined);
      transcriber->Transcribe(
          TranscribeRequest{.pcm = pcm, .language = language, .glossary = options.glossary},
          [&](TranscribedWords transcribed) {
            if (!transcribed.words.empty()) {
              if (auto segment = ToSegment(std::move(transcribed.words),
                                           std::move(transcribed.language), joined)) {
                done.segments.push_back(std::move(*segment));
                Show(on_segments, index, false, std::span(&done.segments.back(), 1));
              }
            } else if (!transcribed.text.empty()) {
              if (!done.text.empty()) throw std::logic_error("a transcriber gave one chunk's text twice");
              done.text = std::move(transcribed.text);
              done.language = std::move(transcribed.language);
            }
          },
          [&](double fraction) {
            const double part = static_cast<double>(done_samples) +
                                std::clamp(fraction, 0.0, 1.0) * static_cast<double>(joined.size());
            progress(Stage::kTranscribe, part / static_cast<double>(total_samples));
          },
          stop);
      run.Add(Stage::kTranscribe, index, done);
    }
    if (!done.text.empty()) {
      result.heard.push_back({.index = index, .chunk = chunk, .text = done.text, .language = done.language});
      const Segment draft = Draft(result.heard.back());
      Show(on_segments, index, true, std::span(&draft, 1));
    }
    result.segments.insert(result.segments.end(), std::make_move_iterator(done.segments.begin()),
                           std::make_move_iterator(done.segments.end()));
    done_samples += joined.size();
    progress(Stage::kTranscribe, static_cast<double>(done_samples) / static_cast<double>(total_samples));
  }
  if (transcriber) {
    transcript.models["transcription"] = transcriber->Info();
  } else {
    run.SavedModel("transcription", transcript);
  }
  return result;
}

// The alignment stage: word times for what the transcriber heard, sentence by
// sentence as the transcript shows them.
std::vector<Segment> AlignSpeech(const AudioBuffer& audio, const std::vector<Heard>& heard,
                                 ModelProvider& models, const ProgressCallback& progress,
                                 const SegmentsCallback& on_segments, const StopToken& stop,
                                 const Resumption& run, Transcript& transcript) {
  progress(Stage::kAlign, 0.0);  // before the model loads
  ThrowIfStopped(stop);
  std::unique_ptr<Aligner> aligner;
  const auto open = [&] {
    aligner = models.OpenAligner();
    if (!aligner) throw std::logic_error("the transcriber left out word times and there is no aligner");
    if (run.sink != nullptr) run.sink->AddModel("alignment", aligner->Info());
  };

  std::vector<Segment> segments;
  SampleIndex total_samples = 0;
  SampleIndex done_samples = 0;
  for (const Heard& item : heard) total_samples += JoinedSpeech(item.chunk).size();

  for (const Heard& item : heard) {
    ThrowIfStopped(stop);
    const JoinedSpeech joined(item.chunk);
    ChunkResult done;
    if (const ChunkResult* saved = run.Saved(Stage::kAlign, item.index)) {
      done = *saved;
    } else {
      if (!aligner) open();
      const std::vector<float> pcm = ReadJoined(audio, item.chunk, joined);
      std::vector<Word> words = aligner->Align(
          AlignRequest{.pcm = pcm, .text = item.text, .language = item.language},
          [&](double fraction) {
            const double part = static_cast<double>(done_samples) +
                                std::clamp(fraction, 0.0, 1.0) * static_cast<double>(joined.size());
            progress(Stage::kAlign, part / static_cast<double>(total_samples));
          },
          stop);
      std::vector<Word> sentence;
      const auto add = [&] {
        if (auto segment = ToSegment(std::exchange(sentence, {}), item.language, joined)) {
          done.segments.push_back(std::move(*segment));
        }
      };
      for (Word& word : words) {
        const bool ends = EndsSentence(word.text);
        sentence.push_back(std::move(word));
        if (ends) add();
      }
      if (!sentence.empty()) add();
      run.Add(Stage::kAlign, item.index, done);
    }
    // All of the chunk at once, to replace its draft.
    Show(on_segments, item.index, false, done.segments);
    segments.insert(segments.end(), std::make_move_iterator(done.segments.begin()),
                    std::make_move_iterator(done.segments.end()));
    done_samples += joined.size();
    progress(Stage::kAlign, static_cast<double>(done_samples) / static_cast<double>(total_samples));
  }
  if (aligner) {
    transcript.models["alignment"] = aligner->Info();
  } else {
    run.SavedModel("alignment", transcript);
  }
  return segments;
}

std::vector<SpeakerTurn> Diarize(const AudioBuffer& audio,
                                 ModelProvider& models,
                                 const ProgressCallback& progress,
                                 const StopToken& stop,
                                 Transcript& transcript) {
  progress(Stage::kDiarize,
           0.0);  // before the model loads, which takes a while
  ThrowIfStopped(stop);
  const auto diarizer = models.OpenDiarizer();
  for (SampleIndex begin = 0; begin < audio.size();
       begin += kDiarizationChunk) {
    ThrowIfStopped(stop);
    diarizer->Push(audio.Read({begin, begin + kDiarizationChunk}));
    progress(Stage::kDiarize, static_cast<double>(std::min(
                                  begin + kDiarizationChunk, audio.size())) /
                                  static_cast<double>(audio.size()));
  }
  ThrowIfStopped(stop);
  std::vector<SpeakerTurn> turns = diarizer->Finish();
  transcript.models["diarization"] = diarizer->Info();
  return turns;
}

}  // namespace

Transcript RunPipeline(const AudioBuffer& audio, const PipelineOptions& options,
                       ModelProvider& models, const ProgressCallback& progress,
                       StopToken stop, const SegmentsCallback& on_segments,
                       const Checkpoint* resume, CheckpointSink* checkpoints,
                       SpeechStore* speech_store) {
  if (!audio.finished()) throw std::logic_error("audio is not finished");
  if (options.language != "auto") {
    const std::vector<std::string> languages = models.Languages();
    if (std::find(languages.begin(), languages.end(), options.language) == languages.end()) {
      throw std::invalid_argument("unsupported language: " + options.language);
    }
  }

  Transcript transcript;
  transcript.duration = ToMilliseconds(audio.size());
  if (audio.size() == 0) return transcript;

  // Before hashing the recording and loading the model, which take a while.
  progress(Stage::kAnalyze, 0.0);
  ThrowIfStopped(stop);  // perhaps from the call above
  const bool checkpointed = resume != nullptr || checkpoints != nullptr;
  const std::string recording =
      checkpointed || speech_store != nullptr ? HashAudio(audio) : "";
  const Speech speech = DetectSpeech(audio, options.segmenter, models, progress, stop,
                                     speech_store, recording, transcript);

  Resumption run{.resume = nullptr, .sink = checkpoints, .key = {}};
  if (checkpointed) {
    run.key = CheckpointKey(recording, options);
    if (resume != nullptr && resume->key == run.key) run.resume = resume;
  }

  // Each stage opens its model in its own function, so the model is freed
  // before the next stage starts.
  Transcribed transcribed;
  if (!speech.regions.empty()) {
    transcribed = TranscribeSpeech(audio, speech, options, models, progress,
                                   on_segments, stop, run, transcript);
  } else {
    progress(Stage::kTranscribe, 0.0);  // every stage starts with 0
  }
  progress(Stage::kTranscribe, 1.0);
  std::vector<Segment> segments = std::move(transcribed.segments);
  if (!transcribed.heard.empty()) {
    segments = AlignSpeech(audio, transcribed.heard, models, progress, on_segments, stop, run,
                           transcript);
    progress(Stage::kAlign, 1.0);
  }

  // Diarization runs on the whole recording each time: it is quick, and its
  // speakers carry over from one part to the next.
  if (options.diarize && !segments.empty()) {
    const std::vector<SpeakerTurn> turns =
        Diarize(audio, models, progress, stop, transcript);
    segments = AssignSpeakers(std::move(segments), turns);
  }
  transcript.segments = SplitSegments(std::move(segments), options.postprocess);
  return transcript;
}

}  // namespace sillok
