#include "core/pipeline.hpp"

#include <algorithm>
#include <cstddef>
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
  ThrowIfStopped(stop);  // perhaps from the call above
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

// Adds `words` of a chunk, timed within it, as a segment timed within the
// recording, and previews it.
void AddSegment(std::vector<Word> words, std::string language, const JoinedSpeech& joined,
                const SegmentsCallback& on_segments, std::vector<Segment>& segments) {
  words = CheckWords(std::move(words), ToMilliseconds(joined.size()));
  if (words.empty()) return;
  for (Word& word : words) {
    word.speaker = std::nullopt;
    std::tie(word.start, word.end) = joined.ToRecording(word.start, word.end);
  }
  segments.push_back(MakeSegment(std::move(words), std::move(language)));
  if (on_segments) on_segments(std::span(&segments.back(), 1));
}

// What a transcriber heard in a chunk without word times, for the aligner.
struct Heard {
  std::vector<SampleRange> chunk;
  std::string text;
  std::string language;
};

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
    const StopToken& stop, Transcript& transcript) {
  progress(Stage::kTranscribe,
           0.0);  // before the model loads, which takes a while
  ThrowIfStopped(stop);
  const auto transcriber = models.OpenTranscriber();
  const Milliseconds longest = std::min(options.segmenter.max_chunk, transcriber->MaxAudio());
  transcriber->CheckGlossary(options.glossary, longest);
  const std::vector<std::vector<SampleRange>> chunks =
      ChunkSpeech(speech.regions, speech.probabilities, speech.frame_samples, longest);
  // Each chunk's language is told from its own speech: a recording may change
  // language between chunks.
  const std::string language = options.language == "auto" ? "" : options.language;

  Transcribed result;
  SampleIndex total_samples = 0;
  SampleIndex done_samples = 0;
  for (const std::vector<SampleRange>& chunk : chunks) total_samples += JoinedSpeech(chunk).size();

  for (const std::vector<SampleRange>& chunk : chunks) {
    ThrowIfStopped(stop);
    const JoinedSpeech joined(chunk);
    const std::vector<float> pcm = ReadJoined(audio, chunk, joined);
    transcriber->Transcribe(
        TranscribeRequest{.pcm = pcm, .language = language, .glossary = options.glossary},
        [&](TranscribedWords transcribed) {
          if (!transcribed.words.empty()) {
            AddSegment(std::move(transcribed.words), std::move(transcribed.language), joined,
                       on_segments, result.segments);
          } else if (!transcribed.text.empty()) {
            result.heard.push_back({.chunk = chunk,
                                    .text = std::move(transcribed.text),
                                    .language = std::move(transcribed.language)});
          }
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
  return result;
}

// The alignment stage: word times for what the transcriber heard, sentence by
// sentence as the transcript shows them.
std::vector<Segment> AlignSpeech(const AudioBuffer& audio, const std::vector<Heard>& heard,
                                 ModelProvider& models, const ProgressCallback& progress,
                                 const SegmentsCallback& on_segments, const StopToken& stop,
                                 Transcript& transcript) {
  progress(Stage::kAlign, 0.0);  // before the model loads
  ThrowIfStopped(stop);
  const auto aligner = models.OpenAligner();
  if (!aligner) throw std::logic_error("the transcriber left out word times and there is no aligner");

  std::vector<Segment> segments;
  SampleIndex total_samples = 0;
  SampleIndex done_samples = 0;
  for (const Heard& item : heard) total_samples += JoinedSpeech(item.chunk).size();

  for (const Heard& item : heard) {
    ThrowIfStopped(stop);
    const JoinedSpeech joined(item.chunk);
    const std::vector<float> pcm = ReadJoined(audio, item.chunk, joined);
    std::vector<Word> words = aligner->Align(
        AlignRequest{.pcm = pcm, .text = item.text, .language = item.language},
        [&](double fraction) {
          const double done = static_cast<double>(done_samples) +
                              std::clamp(fraction, 0.0, 1.0) * static_cast<double>(joined.size());
          progress(Stage::kAlign, done / static_cast<double>(total_samples));
        },
        stop);
    std::vector<Word> sentence;
    for (Word& word : words) {
      const bool ends = EndsSentence(word.text);
      sentence.push_back(std::move(word));
      if (ends) AddSegment(std::exchange(sentence, {}), item.language, joined, on_segments, segments);
    }
    if (!sentence.empty()) AddSegment(std::move(sentence), item.language, joined, on_segments, segments);
    done_samples += joined.size();
  }
  transcript.models["alignment"] = aligner->Info();
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
  Transcribed transcribed;
  if (!speech.regions.empty()) {
    transcribed = TranscribeSpeech(audio, speech, options, models, progress,
                                   on_segments, stop, transcript);
  } else {
    progress(Stage::kTranscribe, 0.0);  // every stage starts with 0
  }
  progress(Stage::kTranscribe, 1.0);
  std::vector<Segment> segments = std::move(transcribed.segments);
  if (!transcribed.heard.empty()) {
    segments = AlignSpeech(audio, transcribed.heard, models, progress, on_segments, stop,
                           transcript);
    progress(Stage::kAlign, 1.0);
  }

  if (options.diarize && !segments.empty()) {
    const std::vector<SpeakerTurn> turns =
        Diarize(audio, models, progress, stop, transcript);
    segments = AssignSpeakers(std::move(segments), turns);
  }
  transcript.segments = SplitSegments(std::move(segments), options.postprocess);
  return transcript;
}

}  // namespace sillok
