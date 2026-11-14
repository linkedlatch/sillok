#include "core/pipeline.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

#include "core/audio.hpp"
#include "core/cancel.hpp"
#include "core/diarizer.hpp"
#include "core/transcriber.hpp"
#include "core/words.hpp"

namespace sillok {
namespace {

constexpr SampleIndex kDiarizationChunk = SampleIndex{kSampleRate} * 10;

// The model hears the whole recording and reads it as its own tools do.
std::vector<Segment> TranscribeAudio(const AudioBuffer& audio, const PipelineOptions& options,
                                     ModelProvider& models, const ProgressCallback& progress,
                                     const SegmentsCallback& on_segments, const StopToken& stop,
                                     Transcript& transcript) {
  progress(Stage::kTranscribe,
           0.0);  // before the model loads, which takes a while
  const auto transcriber = models.OpenTranscriber();
  const std::string language = options.language == "auto" ? "" : options.language;
  std::vector<Segment> segments;
  double done = 0.0;
  transcriber->Transcribe(
      TranscribeRequest{.audio = audio, .language = language, .glossary = options.glossary},
      [&](TranscribedWords transcribed) {
        std::vector<Word> words =
            CheckWords(std::move(transcribed.words), ToMilliseconds(audio.size()));
        if (words.empty()) return;
        for (Word& word : words) word.speaker = std::nullopt;
        segments.push_back(MakeSegment(std::move(words), std::move(transcribed.language)));
        if (on_segments) on_segments(std::span(&segments.back(), 1));
      },
      [&](double fraction) {
        done = std::max(done, std::clamp(fraction, 0.0, 1.0));
        progress(Stage::kTranscribe, done);
      },
      stop);
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

  // Each stage opens its model in its own function, so the model is freed
  // before the next stage starts.
  std::vector<Segment> segments =
      TranscribeAudio(audio, options, models, progress, on_segments, stop, transcript);
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
