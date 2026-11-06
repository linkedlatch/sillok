#include <gtest/gtest.h>

#include <map>
#include <string>
#include <thread>
#include <vector>

#include "core/cancel.hpp"
#include "core/pipeline.hpp"
#include "fakes/fake_models.hpp"

namespace sillok {
namespace {

using testing::FakeModels;
using testing::FakeOptions;
using testing::RenderScript;
using testing::ScriptWord;
using testing::WordText;

// Continuous speech: words of 300 ms with 100 ms gaps from `start`.
std::vector<ScriptWord> Speech(int first_id, int count, Milliseconds start,
                               const std::string& language = "en") {
  std::vector<ScriptWord> words;
  for (int i = 0; i < count; ++i) {
    const Milliseconds begin = start + Milliseconds{400 * i};
    words.push_back({.id = first_id + i,
                     .start = begin,
                     .end = begin + Milliseconds{300},
                     .language = language,
                     .speaker = 0});
  }
  return words;
}

std::vector<std::string> AllWords(const Transcript& transcript) {
  std::vector<std::string> words;
  for (const Segment& segment : transcript.segments) {
    for (const Word& word : segment.words) words.push_back(word.text);
  }
  return words;
}

std::vector<std::string> Expected(const std::vector<ScriptWord>& script) {
  std::vector<std::string> words;
  for (const ScriptWord& word : script) {
    words.push_back(WordText(word.id, word.language));
  }
  return words;
}

FakeOptions OptionsFor(const std::vector<ScriptWord>& script) {
  FakeOptions options;
  for (const ScriptWord& word : script) {
    options.languages[word.id] = word.language;
    options.speakers[word.id] = word.speaker;
  }
  return options;
}

Transcript RunFakes(const AudioBuffer& audio, FakeModels& models,
               const PipelineOptions& options = {},
               const ProgressCallback& progress = [](Stage, double) {},
               StopToken stop = {}) {
  return RunPipeline(audio, options, models, progress, stop);
}

TEST(PipelineTest, TranscribesSpeechSeparatedByLongPausesSeparately) {
  // A minute without a long pause goes in one request; the speech after the
  // pause in another.
  auto script = Speech(0, 150, Milliseconds{500});
  const auto after = Speech(200, 10, Milliseconds{70000});
  script.insert(script.end(), after.begin(), after.end());
  const AudioBuffer audio = RenderScript(script, Milliseconds{75000});
  FakeModels models(OptionsFor(script));
  const Transcript transcript = RunFakes(audio, models);
  EXPECT_EQ(models.transcribe_calls(), 2);
  EXPECT_EQ(AllWords(transcript), Expected(script));
}

TEST(PipelineTest, LeavesPausesOutOfWhatTheModelHears) {
  // A pause of a second, under the gap that separates requests.
  auto script = Speech(0, 5, Milliseconds{500});
  const auto after = Speech(10, 5, Milliseconds{3500});
  script.insert(script.end(), after.begin(), after.end());
  std::size_t heard = 0;
  FakeOptions options = OptionsFor(script);
  options.on_transcribe = [&](const TranscribeRequest& request) { heard += request.pcm.size(); };
  FakeModels models(options);
  const Transcript transcript = RunFakes(RenderScript(script, Milliseconds{6000}), models);
  EXPECT_EQ(models.transcribe_calls(), 1);
  EXPECT_LT(heard, static_cast<std::size_t>(ToSamples(Milliseconds{4500})));
  EXPECT_EQ(AllWords(transcript), Expected(script));
  // The last word is back at its time in the recording.
  const Word& last = transcript.segments.back().words.back();
  EXPECT_NEAR(static_cast<double>(last.start.count()), 3500.0 + 4 * 400.0, 40.0);
}

TEST(PipelineTest, TimesAreInTheRecordingAndOrdered) {
  const auto script = Speech(0, 20, Milliseconds{30000});
  FakeModels models(OptionsFor(script));
  const Transcript transcript = RunFakes(RenderScript(script, Milliseconds{45000}), models);
  ASSERT_FALSE(transcript.segments.empty());
  EXPECT_EQ(transcript.duration, Milliseconds{45000});
  Milliseconds last{0};
  for (const Segment& segment : transcript.segments) {
    for (const Word& word : segment.words) {
      EXPECT_GE(word.start, last);
      EXPECT_LE(word.start, word.end);
      last = word.start;
    }
  }
  // The first word starts at 30 s, within the fake's +-40 ms jitter.
  EXPECT_NEAR(static_cast<double>(transcript.segments[0].start.count()), 30000.0, 40.0);
}

TEST(PipelineTest, SilenceProducesNoTranscriptionModel) {
  FakeModels models(FakeOptions{});
  const Transcript transcript = RunFakes(RenderScript({}, Milliseconds{10000}), models);
  EXPECT_TRUE(transcript.segments.empty());
  EXPECT_EQ(models.transcribe_calls(), 0);
  EXPECT_EQ(transcript.models.count("transcription"), 0u);
}

TEST(PipelineTest, TranscriberAndDiarizerNeverShareMemory) {
  auto script = Speech(0, 10, Milliseconds{1000});
  auto second = Speech(10, 10, Milliseconds{6000});
  for (ScriptWord& word : second) word.speaker = 1;
  script.insert(script.end(), second.begin(), second.end());
  FakeModels models(OptionsFor(script));
  PipelineOptions options;
  options.diarize = true;
  const Transcript transcript = RunFakes(RenderScript(script, Milliseconds{12000}), models, options);
  EXPECT_FALSE(models.live().transcriber_with_diarizer);
  EXPECT_EQ(models.live().transcribers, 0);
  EXPECT_EQ(models.live().diarizers, 0);
  EXPECT_EQ(models.live().detectors, 0);
  ASSERT_GE(transcript.segments.size(), 2u);
  EXPECT_EQ(transcript.segments.front().speaker, 0);
  EXPECT_EQ(transcript.segments.back().speaker, 1);
  EXPECT_EQ(transcript.models.count("diarization"), 1u);
}

TEST(PipelineTest, ReportsProgressInStageOrder) {
  const auto script = Speech(0, 100, Milliseconds{500});
  FakeModels models(OptionsFor(script));
  PipelineOptions options;
  options.diarize = true;
  std::vector<std::pair<Stage, double>> events;
  RunFakes(RenderScript(script, Milliseconds{45000}), models, options,
      [&](Stage stage, double fraction) { events.emplace_back(stage, fraction); });
  ASSERT_FALSE(events.empty());
  for (std::size_t i = 1; i < events.size(); ++i) {
    const auto& [stage, fraction] = events[i];
    const auto& [previous_stage, previous_fraction] = events[i - 1];
    ASSERT_GE(static_cast<int>(stage), static_cast<int>(previous_stage));
    if (stage == previous_stage) {
      EXPECT_GE(fraction, previous_fraction);
    }
    EXPECT_GE(fraction, 0.0);
    EXPECT_LE(fraction, 1.0);
  }
  // Each stage is announced as it starts, before its model loads.
  std::map<Stage, double> first;
  for (const auto& [stage, fraction] : events) first.try_emplace(stage, fraction);
  EXPECT_EQ(first.size(), 3u);
  for (const auto& [stage, fraction] : first) EXPECT_EQ(fraction, 0.0);
  std::map<Stage, double> last;
  for (const auto& [stage, fraction] : events) last[stage] = fraction;
  EXPECT_DOUBLE_EQ(last[Stage::kAnalyze], 1.0);
  EXPECT_DOUBLE_EQ(last[Stage::kTranscribe], 1.0);
  EXPECT_DOUBLE_EQ(last[Stage::kDiarize], 1.0);
}

TEST(PipelineTest, CancelStopsDuringTranscriptionAndFreesModels) {
  const auto script = Speech(0, 150, Milliseconds{500});
  FakeModels models(OptionsFor(script));
  StopSource source;
  int windows_after_cancel = 0;
  const auto progress = [&](Stage stage, double) {
    if (stage == Stage::kTranscribe) {
      if (source.StopRequested()) ++windows_after_cancel;
      source.RequestStop();
    }
  };
  EXPECT_THROW(RunFakes(RenderScript(script, Milliseconds{61000}), models, {}, progress,
                   source.Token()),
               Cancelled);
  EXPECT_EQ(windows_after_cancel, 0);
  EXPECT_EQ(models.live().transcribers, 0);
}

TEST(PipelineTest, CancelFromAnotherThread) {
  const auto script = Speech(0, 150, Milliseconds{500});
  FakeOptions options = OptionsFor(script);
  StopSource source;
  options.on_transcribe = [&](const TranscribeRequest&) {
    std::thread([&] { source.RequestStop(); }).join();
  };
  FakeModels models(options);
  EXPECT_THROW(RunFakes(RenderScript(script, Milliseconds{61000}), models, {},
                   [](Stage, double) {}, source.Token()),
               Cancelled);
  EXPECT_EQ(models.transcribe_calls(), 1);
}

TEST(PipelineTest, DetectsTheLanguageOnceFromTheFirstSpeech) {
  // A minute of silence, 40 s of Korean, then English: one language.
  auto script = Speech(0, 100, Milliseconds{60000}, "ko");
  const auto english = Speech(200, 20, Milliseconds{100000}, "en");
  script.insert(script.end(), english.begin(), english.end());
  FakeModels models(OptionsFor(script));
  const Transcript transcript = RunFakes(RenderScript(script, Milliseconds{110000}), models);
  ASSERT_FALSE(transcript.segments.empty());
  for (const Segment& segment : transcript.segments) EXPECT_EQ(segment.language, "ko");
}

TEST(PipelineTest, FixedLanguageSkipsDetection) {
  const auto script = Speech(0, 10, Milliseconds{500}, "ko");
  FakeOptions options = OptionsFor(script);
  options.detects_language = false;
  FakeModels models(options);
  PipelineOptions pipeline;
  pipeline.language = "ko";
  const Transcript transcript = RunFakes(RenderScript(script, Milliseconds{6000}), models, pipeline);
  EXPECT_EQ(AllWords(transcript), Expected(script));

  FakeModels no_detection(options);
  EXPECT_THROW(RunFakes(RenderScript(script, Milliseconds{6000}), no_detection),
               std::invalid_argument);
  pipeline.language = "fr";
  EXPECT_THROW(
      RunFakes(RenderScript(script, Milliseconds{6000}), models, pipeline),
      std::invalid_argument);
}

TEST(PipelineTest, PromptIsTheGlossary) {
  auto script = Speech(0, 5, Milliseconds{500});
  const auto after = Speech(10, 5, Milliseconds{10000});
  script.insert(script.end(), after.begin(), after.end());
  FakeOptions options = OptionsFor(script);
  std::vector<std::string> prompts;
  options.on_transcribe = [&](const TranscribeRequest& request) { prompts.push_back(request.prompt); };
  FakeModels models(options);
  PipelineOptions pipeline;
  pipeline.glossary = {"sillok", "Whisper"};
  RunFakes(RenderScript(script, Milliseconds{13000}), models, pipeline);
  EXPECT_EQ(prompts, (std::vector<std::string>{"sillok, Whisper", "sillok, Whisper"}));
}

}  // namespace
}  // namespace sillok
