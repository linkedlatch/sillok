#include "core/pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "core/audio.hpp"
#include "core/cancel.hpp"
#include "core/diarizer.hpp"
#include "core/level.hpp"
#include "core/prompt.hpp"
#include "core/stitcher.hpp"
#include "core/transcriber.hpp"
#include "core/words.hpp"

namespace sillok {
namespace {

constexpr int kEnergyFrame = kSampleRate / 100;  // 10 ms
constexpr SampleIndex kDiarizationChunk = SampleIndex{kSampleRate} * 10;

// Samples of `range` with the recording's gain (LoudnessGain).
std::vector<float> ReadWithGain(const AudioBuffer& audio, SampleRange range,
                                double gain) {
  std::vector<float> pcm = audio.Read(range);
  ApplyGain(gain, pcm);
  return pcm;
}

struct Analysis {
  std::vector<float> probabilities;  // one per voice-detection frame
  int frame_samples = 0;
};

Analysis Analyze(const AudioBuffer& audio, double gain, ModelProvider& models,
                 const ProgressCallback& progress, const StopToken& stop,
                 Transcript& transcript) {
  progress(Stage::kAnalyze,
           0.0);  // before the model loads, which takes a while
  const auto detector = models.OpenVoiceDetector();
  Analysis analysis{.probabilities = {},
                    .frame_samples = detector->FrameSamples()};
  // About 30 s per call, in whole frames so the detector state carries over.
  const SampleIndex chunk =
      std::max<SampleIndex>(
          1, SampleIndex{kSampleRate} * 30 / analysis.frame_samples) *
      analysis.frame_samples;
  for (SampleIndex begin = 0; begin < audio.size(); begin += chunk) {
    ThrowIfStopped(stop);
    const std::vector<float> probabilities =
        detector->Detect(ReadWithGain(audio, {begin, begin + chunk}, gain));
    analysis.probabilities.insert(analysis.probabilities.end(),
                                  probabilities.begin(), probabilities.end());
    progress(Stage::kAnalyze,
             static_cast<double>(std::min(begin + chunk, audio.size())) /
                 static_cast<double>(audio.size()));
  }
  transcript.models["voice_detection"] = detector->Info();
  return analysis;
}

double MeanLogProbability(const std::vector<TokenSegment>& segments) {
  double sum = 0.0;
  std::size_t count = 0;
  for (const TokenSegment& segment : segments) {
    for (const Token& token : segment.tokens) {
      sum += std::log(std::max(static_cast<double>(token.probability), 1e-6));
      ++count;
    }
  }
  return count == 0 ? std::numeric_limits<double>::lowest()
                    : sum / static_cast<double>(count);
}

// Segments of one window, with times in the recording clamped to the audio
// the window heard.
std::vector<Segment> ToSegments(const std::vector<TokenSegment>& decoded,
                                const Window& window,
                                const std::string& language) {
  const Milliseconds begin = ToMilliseconds(window.audio.begin);
  const Milliseconds end = ToMilliseconds(window.audio.end);
  std::vector<Segment> segments;
  for (const TokenSegment& part : decoded) {
    std::vector<Word> words = AssembleWords(part.tokens, begin, language);
    for (Word& word : words) {
      word.start = std::clamp(word.start, begin, end);
      word.end = std::clamp(word.end, word.start, end);
    }
    if (!words.empty())
      segments.push_back(MakeSegment(std::move(words), language));
  }
  return segments;
}

std::vector<Word> Flatten(const std::vector<Segment>& segments) {
  std::vector<Word> words;
  for (const Segment& segment : segments) {
    words.insert(words.end(), segment.words.begin(), segment.words.end());
  }
  return words;
}

// What a finished window leaves for the next one to use as prompt context.
struct Context {
  std::vector<Word> words;
  std::string language;
  bool usable = false;
};

std::string ContextText(const Context& context, const std::string& language,
                        Milliseconds before) {
  if (!context.usable || context.language != language) return {};
  std::vector<Word> words;
  for (const Word& word : context.words) {
    // Words the next window hears again must not be in its prompt, or the
    // model may take them as already said and skip them.
    if (word.end <= before) words.push_back(word);
  }
  return JoinWords(words, language);
}

std::vector<Segment> TranscribeSpeech(
    const AudioBuffer& audio, double gain, std::span<const SampleRange> speech,
    const PipelineOptions& options, ModelProvider& models,
    const ProgressCallback& progress, const SegmentsCallback& on_segments,
    const StopToken& stop, Transcript& transcript) {
  progress(Stage::kTranscribe,
           0.0);  // before the model loads, which takes a while
  const auto transcriber = models.OpenTranscriber();
  const TranscriberCapabilities capabilities = transcriber->Capabilities();
  // Windows never hold more audio than the model accepts.
  SegmenterOptions segmenter = options.segmenter;
  if (capabilities.max_audio > Milliseconds{0}) {
    segmenter.max_window =
        std::min(segmenter.max_window, capabilities.max_audio);
  }
  const std::vector<Window> windows =
      PlanWindows(speech, audio.FrameEnergy(kEnergyFrame), kEnergyFrame,
                  audio.size(), segmenter);

  const bool detect_language = options.language == "auto";
  if (detect_language && !capabilities.detects_language) {
    throw std::invalid_argument(
        "the transcription model cannot detect languages; choose one");
  }
  LanguageStabilizer stabilizer(options.candidates, options.language_detection);
  std::optional<PromptBuilder> prompts;
  if (capabilities.max_prompt_tokens > 0) {
    prompts.emplace(
        options.glossary, capabilities.max_prompt_tokens,
        [&](std::string_view text) { return transcriber->CountTokens(text); });
  }
  const auto decode = [&](const std::vector<float>& pcm,
                          const std::string& language,
                          const std::string& context) {
    return transcriber->Transcribe(
        TranscribeRequest{.pcm = pcm,
                          .language = language,
                          .prompt = prompts ? prompts->Build(context, language)
                                            : std::string()},
        stop);
  };

  SampleIndex total_samples = 0;
  for (const Window& window : windows) total_samples += window.audio.length();
  SampleIndex done_samples = 0;

  std::vector<Segment> finished;
  std::vector<Segment> previous;  // may still be cut by an overlapping window
  const Window* previous_window = nullptr;
  Context context;
  for (const Window& window : windows) {
    ThrowIfStopped(stop);
    const std::vector<float> pcm = ReadWithGain(audio, window.audio, gain);
    LanguageDecision decision{.language = options.language,
                              .alternative = std::nullopt,
                              .overruled = false};
    if (detect_language) {
      decision = stabilizer.Decide(
          transcriber->DetectLanguage(pcm, options.candidates, stop));
    }
    const Milliseconds window_begin = ToMilliseconds(window.audio.begin);
    std::string language = decision.language;
    std::vector<TokenSegment> decoded =
        decode(pcm, language, ContextText(context, language, window_begin));
    double quality = MeanLogProbability(decoded);
    if (decision.alternative &&
        (decision.overruled || quality < options.retry_below)) {
      const std::string& other = *decision.alternative;
      std::vector<TokenSegment> retry =
          decode(pcm, other, ContextText(context, other, window_begin));
      const double retry_quality = MeanLogProbability(retry);
      if (retry_quality > quality) {
        decoded = std::move(retry);
        quality = retry_quality;
        language = other;
      }
    }
    if (detect_language) stabilizer.Commit(language);

    std::vector<Segment> segments = ToSegments(decoded, window, language);
    if (window.overlaps_previous && previous_window != nullptr) {
      const std::vector<Word> earlier = Flatten(previous);
      const std::vector<Word> later = Flatten(segments);
      const StitchCut cut =
          FindStitch(earlier, later, window_begin,
                     ToMilliseconds(previous_window->audio.end),
                     ToMilliseconds(window.owned.begin));
      previous = KeepLeadingWords(std::move(previous), cut.earlier_keep);
      segments = DropLeadingWords(std::move(segments), cut.later_skip);
    }
    // The next window can no longer cut the previous one's segments.
    if (on_segments && !previous.empty()) on_segments(previous);
    std::move(previous.begin(), previous.end(), std::back_inserter(finished));

    const bool switched =
        !context.language.empty() && language != context.language;
    context =
        Context{.words = Flatten(segments),
                .language = language,
                .usable = !switched && quality >= options.min_context_quality};
    previous = std::move(segments);
    previous_window = &window;

    done_samples += window.audio.length();
    progress(Stage::kTranscribe, static_cast<double>(done_samples) /
                                     static_cast<double>(total_samples));
  }
  if (on_segments && !previous.empty()) on_segments(previous);
  std::move(previous.begin(), previous.end(), std::back_inserter(finished));
  transcript.models["transcription"] = transcriber->Info();
  return finished;
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
  const Analysis analysis =
      Analyze(audio, gain, models, progress, stop, transcript);
  const std::vector<SampleRange> speech =
      FindSpeech(analysis.probabilities, analysis.frame_samples, audio.size(),
                 options.segmenter);

  // Each stage opens its model in its own function, so the model is freed
  // before the next stage starts.
  std::vector<Segment> segments;
  if (!speech.empty()) {
    segments = TranscribeSpeech(audio, gain, speech, options, models, progress,
                                on_segments, stop, transcript);
  }
  progress(Stage::kTranscribe, 1.0);
  segments =
      RemoveSilenceHallucinations(std::move(segments), analysis.probabilities,
                                  analysis.frame_samples, options.postprocess);

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
