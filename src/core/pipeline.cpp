#include "core/pipeline.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>

#include "core/audio.hpp"
#include "core/cancel.hpp"
#include "core/diarizer.hpp"
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

Speech DetectSpeech(const AudioBuffer& audio, const SegmenterOptions& options,
                    ModelProvider& models, const ProgressCallback& progress,
                    const StopToken& stop, Transcript& transcript) {
  progress(Stage::kAnalyze,
           0.0);  // before the model loads, which takes a while
  const auto detector = models.OpenVoiceDetector();
  Speech speech{.regions = {}, .probabilities = {}, .frame_samples = detector->FrameSamples()};
  // About 30 s per call, in whole frames so the detector state carries over.
  const SampleIndex chunk =
      std::max<SampleIndex>(1, SampleIndex{kSampleRate} * 30 / speech.frame_samples) *
      speech.frame_samples;
  for (SampleIndex begin = 0; begin < audio.size(); begin += chunk) {
    ThrowIfStopped(stop);
    const std::vector<float> part =
        detector->Detect(audio.Read({begin, begin + chunk}));
    speech.probabilities.insert(speech.probabilities.end(), part.begin(), part.end());
    progress(Stage::kAnalyze,
             static_cast<double>(std::min(begin + chunk, audio.size())) /
                 static_cast<double>(audio.size()));
  }
  transcript.models["voice_detection"] = detector->Info();
  speech.regions = FindSpeech(speech.probabilities, speech.frame_samples, audio.size(), options);
  return speech;
}

std::vector<Segment> TranscribeSpeech(
    const AudioBuffer& audio, const Speech& speech,
    const PipelineOptions& options, ModelProvider& models,
    const ProgressCallback& progress, const SegmentsCallback& on_segments,
    const StopToken& stop, Transcript& transcript) {
  progress(Stage::kTranscribe,
           0.0);  // before the model loads, which takes a while
  const auto transcriber = models.OpenTranscriber();
  const std::vector<std::vector<SampleRange>> groups =
      ChunkSpeech(speech.regions, speech.probabilities, speech.frame_samples,
                  std::min(options.segmenter.max_chunk, transcriber->MaxAudio()));
  // Each chunk's language is told from its own speech: a recording may change
  // language between chunks.
  const std::string language = options.language == "auto" ? "" : options.language;

  std::vector<Segment> segments;
  SampleIndex total_samples = 0;
  SampleIndex done_samples = 0;
  for (const std::vector<SampleRange>& group : groups) total_samples += JoinedSpeech(group).size();

  for (const std::vector<SampleRange>& group : groups) {
    ThrowIfStopped(stop);
    const JoinedSpeech joined(group);
    std::vector<float> pcm(static_cast<std::size_t>(joined.size()), 0.0f);
    for (std::size_t i = 0; i < group.size(); ++i) {
      const std::vector<float> part = audio.Read(group[i]);
      std::copy(part.begin(), part.end(),
                pcm.begin() + static_cast<std::ptrdiff_t>(joined.Offset(i)));
    }
    transcriber->Transcribe(
        TranscribeRequest{.pcm = pcm, .language = language, .glossary = options.glossary},
        [&](TranscribedWords transcribed) {
          std::vector<Word> words =
              CheckWords(std::move(transcribed.words), ToMilliseconds(joined.size()));
          if (words.empty()) return;
          for (Word& word : words) {
            word.speaker = std::nullopt;
            std::tie(word.start, word.end) = joined.ToRecording(word.start, word.end);
          }
          segments.push_back(MakeSegment(std::move(words), std::move(transcribed.language)));
          if (on_segments) on_segments(std::span(&segments.back(), 1));
        },
        [&](double fraction) {
          const double done = static_cast<double>(done_samples) +
                              std::clamp(fraction, 0.0, 1.0) * static_cast<double>(joined.size());
          progress(Stage::kTranscribe, done / static_cast<double>(total_samples));
        },
        stop);
    done_samples += joined.size();
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
                       StopToken stop, const SegmentsCallback& on_segments) {
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

  const Speech speech = DetectSpeech(
      audio, options.segmenter, models, progress, stop, transcript);

  // Each stage opens its model in its own function, so the model is freed
  // before the next stage starts.
  std::vector<Segment> segments;
  if (!speech.regions.empty()) {
    segments = TranscribeSpeech(audio, speech, options, models, progress,
                                on_segments, stop, transcript);
  }
  progress(Stage::kTranscribe, 1.0);

  if (options.diarize && !segments.empty()) {
    const std::vector<SpeakerTurn> turns =
        Diarize(audio, models, progress, stop, transcript);
    segments = AssignSpeakers(std::move(segments), turns);
  }
  transcript.segments = SplitSegments(std::move(segments), options.postprocess);
  return transcript;
}

}  // namespace sillok
