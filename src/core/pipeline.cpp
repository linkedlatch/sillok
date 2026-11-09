#include "core/pipeline.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>

#include "core/audio.hpp"
#include "core/cancel.hpp"
#include "core/diarizer.hpp"
#include "core/sha256.hpp"
#include "core/transcriber.hpp"
#include "core/words.hpp"

namespace sillok {
namespace {

constexpr SampleIndex kDiarizationChunk = SampleIndex{kSampleRate} * 10;

std::vector<SampleRange> DetectSpeech(const AudioBuffer& audio,
                                      const SegmenterOptions& options,
                                      ModelProvider& models,
                                      const ProgressCallback& progress,
                                      const StopToken& stop,
                                      Transcript& transcript) {
  progress(Stage::kAnalyze,
           0.0);  // before the model loads, which takes a while
  const auto detector = models.OpenVoiceDetector();
  const int frame_samples = detector->FrameSamples();
  std::vector<float> probabilities;
  // About 30 s per call, in whole frames so the detector state carries over.
  const SampleIndex chunk =
      std::max<SampleIndex>(1, SampleIndex{kSampleRate} * 30 / frame_samples) *
      frame_samples;
  for (SampleIndex begin = 0; begin < audio.size(); begin += chunk) {
    ThrowIfStopped(stop);
    const std::vector<float> part =
        detector->Detect(audio.Read({begin, begin + chunk}));
    probabilities.insert(probabilities.end(), part.begin(), part.end());
    progress(Stage::kAnalyze,
             static_cast<double>(std::min(begin + chunk, audio.size())) /
                 static_cast<double>(audio.size()));
  }
  transcript.models["voice_detection"] = detector->Info();
  return FindSpeech(probabilities, frame_samples, audio.size(), options);
}

// Identifies what a checkpoint was made of: the recording, the options that
// shape its transcription and the models that find and transcribe its
// speech. Bump the first line when the transcription of the same input
// changes.
std::string CheckpointKey(const AudioBuffer& audio, const PipelineOptions& options,
                          const std::string& voice_detector,
                          const std::string& transcriber) {
  Sha256 hash;
  const auto add = [&](std::string_view text) {
    hash.Update(std::span(reinterpret_cast<const unsigned char*>(text.data()), text.size()));
    hash.Update(std::span(reinterpret_cast<const unsigned char*>("\n"), 1));
  };
  add("sillok checkpoint 2");
  add(voice_detector);
  add(transcriber);
  add(options.language);
  for (const std::string& term : options.glossary) add(term);
  add("");
  const SegmenterOptions& segmenter = options.segmenter;
  for (const float threshold : {segmenter.speech_threshold, segmenter.silence_threshold}) {
    add(std::to_string(threshold));
  }
  for (const Milliseconds time : {segmenter.min_speech, segmenter.min_silence, segmenter.padding,
                                  segmenter.tail, segmenter.max_gap}) {
    add(std::to_string(time.count()));
  }
  constexpr SampleIndex kBlock = SampleIndex{kSampleRate} * 60;
  for (SampleIndex begin = 0; begin < audio.size(); begin += kBlock) {
    const std::vector<float> part = audio.Read({begin, begin + kBlock});
    hash.Update(std::span(reinterpret_cast<const unsigned char*>(part.data()),
                          part.size() * sizeof(float)));
  }
  return hash.HexDigest();
}

std::string JoinGlossary(const std::vector<std::string>& glossary) {
  std::string joined;
  for (const std::string& term : glossary) {
    if (!joined.empty()) joined += ", ";
    joined += term;
  }
  return joined;
}

std::vector<Segment> TranscribeSpeech(
    const AudioBuffer& audio, std::span<const SampleRange> speech,
    const PipelineOptions& options, ModelProvider& models,
    const ProgressCallback& progress, const SegmentsCallback& on_segments,
    const StopToken& stop, const Checkpoint* resume, CheckpointSink* checkpoints,
    Transcript& transcript) {
  progress(Stage::kTranscribe,
           0.0);  // before the model loads, which takes a while
  const auto transcriber = models.OpenTranscriber();
  const std::vector<std::vector<SampleRange>> groups =
      GroupSpeech(speech, options.segmenter.max_gap);
  const std::string key =
      resume != nullptr || checkpoints != nullptr
          ? CheckpointKey(audio, options, transcript.models.at("voice_detection").name,
                          transcriber->Info().name)
          : std::string();
  const bool continues =
      resume != nullptr && resume->key == key && resume->groups <= groups.size();
  // Each group's language is told from its own start: a recording may change
  // language at a pause.
  const std::string language = options.language == "auto" ? "" : options.language;
  const std::string prompt = JoinGlossary(options.glossary);

  std::vector<Segment> segments;
  std::size_t first = 0;
  if (continues) {
    segments = resume->segments;
    first = resume->groups;
    if (on_segments && !segments.empty()) on_segments(segments);
  } else if (checkpoints != nullptr) {
    checkpoints->Start(key);
  }
  SampleIndex total_samples = 0;
  SampleIndex done_samples = 0;
  for (std::size_t i = 0; i < groups.size(); ++i) {
    const SampleIndex size = JoinedSpeech(groups[i]).size();
    total_samples += size;
    if (i < first) done_samples += size;
  }
  if (first > 0) {
    progress(Stage::kTranscribe,
             static_cast<double>(done_samples) / static_cast<double>(total_samples));
  }

  for (std::size_t index = first; index < groups.size(); ++index) {
    ThrowIfStopped(stop);
    const std::vector<SampleRange>& group = groups[index];
    const std::size_t before = segments.size();
    const JoinedSpeech joined(group);
    std::vector<float> pcm(static_cast<std::size_t>(joined.size()), 0.0f);
    for (std::size_t i = 0; i < group.size(); ++i) {
      const std::vector<float> part = audio.Read(group[i]);
      std::copy(part.begin(), part.end(),
                pcm.begin() + static_cast<std::ptrdiff_t>(joined.Offset(i)));
    }
    transcriber->Transcribe(
        TranscribeRequest{.pcm = pcm, .language = language, .prompt = prompt},
        [&](const TokenSegment& decoded) {
          std::vector<Word> words =
              AssembleWords(decoded.tokens, Milliseconds{0}, decoded.language);
          if (words.empty()) return;
          const Milliseconds heard = words.back().end;
          for (Word& word : words) {
            std::tie(word.start, word.end) =
                joined.ToRecording(word.start, word.end);
          }
          segments.push_back(MakeSegment(std::move(words), decoded.language));
          if (on_segments) on_segments(std::span(&segments.back(), 1));
          const SampleIndex done =
              done_samples + std::min(ToSamples(heard), joined.size());
          progress(Stage::kTranscribe, static_cast<double>(done) /
                                           static_cast<double>(total_samples));
        },
        stop);
    done_samples += joined.size();
    if (checkpoints != nullptr) {
      checkpoints->AddGroup(std::span(segments).subspan(before));
    }
  }
  transcript.models["transcription"] = transcriber->Info();
  return segments;
}

std::vector<SpeakerTurn> Diarize(const AudioBuffer& audio,
                                 ModelProvider& models,
                                 const ProgressCallback& progress,
                                 const StopToken& stop,
                                 Transcript& transcript) {
  progress(Stage::kDiarize,
           0.0);  // before the model loads, which takes a while
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
                       const Checkpoint* resume, CheckpointSink* checkpoints) {
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

  const std::vector<SampleRange> speech = DetectSpeech(
      audio, options.segmenter, models, progress, stop, transcript);

  // Each stage opens its model in its own function, so the model is freed
  // before the next stage starts.
  std::vector<Segment> segments;
  if (!speech.empty()) {
    segments = TranscribeSpeech(audio, speech, options, models, progress,
                                on_segments, stop, resume, checkpoints,
                                transcript);
  }
  progress(Stage::kTranscribe, 1.0);

  if (options.diarize && !segments.empty()) {
    const std::vector<SpeakerTurn> turns =
        Diarize(audio, models, progress, stop, transcript);
    segments = AssignSpeakers(std::move(segments), turns, options.speakers);
  }
  transcript.segments =
      SplitLongSegments(std::move(segments), options.postprocess);
  return transcript;
}

}  // namespace sillok
