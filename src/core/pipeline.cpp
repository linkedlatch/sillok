#include "core/pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "core/audio.hpp"
#include "core/cancel.hpp"
#include "core/diarizer.hpp"
#include "core/level.hpp"
#include "core/transcriber.hpp"
#include "core/words.hpp"

namespace sillok {
namespace {

constexpr SampleIndex kDiarizationChunk = SampleIndex{kSampleRate} * 10;
// Whisper's window, which the language detection hears.
constexpr SampleIndex kLanguageSample = SampleIndex{kSampleRate} * 30;

// Samples of `range` with the recording's gain (LoudnessGain).
std::vector<float> ReadWithGain(const AudioBuffer& audio, SampleRange range,
                                double gain) {
  std::vector<float> pcm = audio.Read(range);
  ApplyGain(gain, pcm);
  return pcm;
}

std::vector<SampleRange> DetectSpeech(const AudioBuffer& audio, double gain,
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
        detector->Detect(ReadWithGain(audio, {begin, begin + chunk}, gain));
    probabilities.insert(probabilities.end(), part.begin(), part.end());
    progress(Stage::kAnalyze,
             static_cast<double>(std::min(begin + chunk, audio.size())) /
                 static_cast<double>(audio.size()));
  }
  transcript.models["voice_detection"] = detector->Info();
  return FindSpeech(probabilities, frame_samples, audio.size(), options);
}

// The most likely of the candidates in the first speech of the recording.
std::string DetectLanguage(const AudioBuffer& audio, double gain,
                           std::span<const SampleRange> speech,
                           const PipelineOptions& options,
                           Transcriber& transcriber, const StopToken& stop) {
  if (!transcriber.DetectsLanguage()) {
    throw std::invalid_argument(
        "the transcription model cannot detect languages; choose one");
  }
  std::vector<float> pcm;
  for (const SampleRange& region : speech) {
    const SampleIndex room = kLanguageSample - static_cast<SampleIndex>(pcm.size());
    if (room <= 0) break;
    const std::vector<float> part = ReadWithGain(
        audio, {region.begin, std::min(region.end, region.begin + room)}, gain);
    pcm.insert(pcm.end(), part.begin(), part.end());
  }
  const std::vector<LanguageScore> scores =
      transcriber.DetectLanguage(pcm, options.candidates, stop);
  return std::max_element(scores.begin(), scores.end(),
                          [](const LanguageScore& a, const LanguageScore& b) {
                            return a.probability < b.probability;
                          })
      ->language;
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
    const AudioBuffer& audio, double gain, std::span<const SampleRange> speech,
    const PipelineOptions& options, ModelProvider& models,
    const ProgressCallback& progress, const SegmentsCallback& on_segments,
    const StopToken& stop, Transcript& transcript) {
  progress(Stage::kTranscribe,
           0.0);  // before the model loads, which takes a while
  const auto transcriber = models.OpenTranscriber();
  const std::string language =
      options.language == "auto"
          ? DetectLanguage(audio, gain, speech, options, *transcriber, stop)
          : options.language;
  const std::string prompt = JoinGlossary(options.glossary);

  const std::vector<std::vector<SampleRange>> groups =
      GroupSpeech(speech, options.segmenter.max_gap);
  SampleIndex total_samples = 0;
  for (const auto& group : groups) total_samples += JoinedSpeech(group).size();
  SampleIndex done_samples = 0;

  std::vector<Segment> segments;
  for (const std::vector<SampleRange>& group : groups) {
    ThrowIfStopped(stop);
    const JoinedSpeech joined(group);
    std::vector<float> pcm(static_cast<std::size_t>(joined.size()), 0.0f);
    for (std::size_t i = 0; i < group.size(); ++i) {
      const std::vector<float> part = ReadWithGain(audio, group[i], gain);
      std::copy(part.begin(), part.end(),
                pcm.begin() + static_cast<std::ptrdiff_t>(joined.Offset(i)));
    }
    transcriber->Transcribe(
        TranscribeRequest{.pcm = pcm, .language = language, .prompt = prompt},
        [&](const TokenSegment& decoded) {
          std::vector<Word> words =
              AssembleWords(decoded.tokens, Milliseconds{0}, language);
          if (words.empty()) return;
          const Milliseconds heard = words.back().end;
          for (Word& word : words) {
            std::tie(word.start, word.end) =
                joined.ToRecording(word.start, word.end);
          }
          segments.push_back(MakeSegment(std::move(words), language));
          if (on_segments) on_segments(std::span(&segments.back(), 1));
          const SampleIndex done =
              done_samples + std::min(ToSamples(heard), joined.size());
          progress(Stage::kTranscribe, static_cast<double>(done) /
                                           static_cast<double>(total_samples));
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
  if (options.language != "auto" &&
      std::find(options.candidates.begin(), options.candidates.end(),
                options.language) == options.candidates.end()) {
    throw std::invalid_argument("unsupported language: " + options.language);
  }

  Transcript transcript;
  transcript.duration = ToMilliseconds(audio.size());
  if (audio.size() == 0) return transcript;

  const double gain = LoudnessGain(audio);
  transcript.gain_db = 20.0 * std::log10(gain);
  const std::vector<SampleRange> speech =
      DetectSpeech(audio, gain, options.segmenter, models, progress, stop,
                   transcript);

  // Each stage opens its model in its own function, so the model is freed
  // before the next stage starts.
  std::vector<Segment> segments;
  if (!speech.empty()) {
    segments = TranscribeSpeech(audio, gain, speech, options, models, progress,
                                on_segments, stop, transcript);
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
