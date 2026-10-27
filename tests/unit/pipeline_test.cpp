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
std::vector<ScriptWord> Speech(int first_id, int count, Millis start,
                               const std::string& language = "en") {
  std::vector<ScriptWord> words;
  for (int i = 0; i < count; ++i) {
    const Millis begin = start + Millis{400 * i};
    words.push_back({.id = first_id + i,
                     .start = begin,
                     .end = begin + Millis{300},
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
               std::stop_token stop = {}) {
  return RunPipeline(audio, options, models, progress, stop);
}

TEST(PipelineTest, LongContinuousSpeechKeepsEveryWordOnce) {
  // 150 words over 60 s without a pause long enough to end a window, so the
  // pipeline must split inside speech with overlapping windows.
  const auto script = Speech(0, 150, Millis{500});
  const AudioBuffer audio = RenderScript(script, Millis{61000});
  int requests = 0;
  FakeOptions options = OptionsFor(script);
  options.on_transcribe = [&](const TranscribeRequest& request) {
    ++requests;
    EXPECT_LE(request.pcm.size(), static_cast<std::size_t>(ToSamples(Millis{28000})));
  };
  FakeModels counting(options);
  const Transcript transcript = RunFakes(audio, counting);
  EXPECT_GE(requests, 3);
  EXPECT_EQ(AllWords(transcript), Expected(script));
}

TEST(PipelineTest, TimesAreInTheRecordingAndOrdered) {
  const auto script = Speech(0, 20, Millis{30000});
  FakeModels models(OptionsFor(script));
  const Transcript transcript = RunFakes(RenderScript(script, Millis{45000}), models);
  ASSERT_FALSE(transcript.segments.empty());
  EXPECT_EQ(transcript.duration, Millis{45000});
  Millis last{0};
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
  const Transcript transcript = RunFakes(RenderScript({}, Millis{10000}), models);
  EXPECT_TRUE(transcript.segments.empty());
  EXPECT_EQ(models.transcribe_calls(), 0);
  EXPECT_EQ(transcript.models.count("transcription"), 0u);
}

TEST(PipelineTest, TranscriberAndDiarizerNeverShareMemory) {
  auto script = Speech(0, 10, Millis{1000});
  auto second = Speech(10, 10, Millis{6000});
  for (ScriptWord& word : second) word.speaker = 1;
  script.insert(script.end(), second.begin(), second.end());
  FakeModels models(OptionsFor(script));
  PipelineOptions options;
  options.diarize = true;
  const Transcript transcript = RunFakes(RenderScript(script, Millis{12000}), models, options);
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
  const auto script = Speech(0, 100, Millis{500});
  FakeModels models(OptionsFor(script));
  PipelineOptions options;
  options.diarize = true;
  std::vector<std::pair<Stage, double>> events;
  RunFakes(RenderScript(script, Millis{45000}), models, options,
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
  std::map<Stage, double> last;
  for (const auto& [stage, fraction] : events) last[stage] = fraction;
  EXPECT_DOUBLE_EQ(last[Stage::kAnalyze], 1.0);
  EXPECT_DOUBLE_EQ(last[Stage::kTranscribe], 1.0);
  EXPECT_DOUBLE_EQ(last[Stage::kDiarize], 1.0);
}

TEST(PipelineTest, CancelStopsDuringTranscriptionAndFreesModels) {
  const auto script = Speech(0, 150, Millis{500});
  FakeModels models(OptionsFor(script));
  std::stop_source source;
  int windows_after_cancel = 0;
  const auto progress = [&](Stage stage, double) {
    if (stage == Stage::kTranscribe) {
      if (source.stop_requested()) ++windows_after_cancel;
      source.request_stop();
    }
  };
  EXPECT_THROW(RunFakes(RenderScript(script, Millis{61000}), models, {}, progress,
                   source.get_token()),
               Cancelled);
  EXPECT_EQ(windows_after_cancel, 0);
  EXPECT_EQ(models.live().transcribers, 0);
}

TEST(PipelineTest, CancelFromAnotherThread) {
  const auto script = Speech(0, 150, Millis{500});
  FakeOptions options = OptionsFor(script);
  std::stop_source source;
  options.on_transcribe = [&](const TranscribeRequest&) {
    std::jthread([&] { source.request_stop(); }).join();
  };
  FakeModels models(options);
  EXPECT_THROW(RunFakes(RenderScript(script, Millis{61000}), models, {},
                   [](Stage, double) {}, source.get_token()),
               Cancelled);
  EXPECT_EQ(models.transcribe_calls(), 1);
}

TEST(PipelineTest, FollowsRealLanguageSwitches) {
  // 20 s of Korean, 20 s of English, then Korean again.
  auto script = Speech(0, 45, Millis{500}, "ko");
  const auto english = Speech(100, 45, Millis{20500}, "en");
  const auto korean = Speech(200, 45, Millis{40500}, "ko");
  script.insert(script.end(), english.begin(), english.end());
  script.insert(script.end(), korean.begin(), korean.end());
  FakeModels models(OptionsFor(script));
  const Transcript transcript = RunFakes(RenderScript(script, Millis{61000}), models);
  EXPECT_EQ(AllWords(transcript), Expected(script));
  for (const Segment& segment : transcript.segments) {
    for (const Word& word : segment.words) {
      EXPECT_EQ(word.text.substr(0, 2), segment.language) << word.text;
    }
  }
}

TEST(PipelineTest, FixedLanguageSkipsDetection) {
  const auto script = Speech(0, 10, Millis{500}, "ko");
  FakeOptions options = OptionsFor(script);
  options.detects_language = false;
  FakeModels models(options);
  PipelineOptions pipeline;
  pipeline.language = "ko";
  const Transcript transcript = RunFakes(RenderScript(script, Millis{6000}), models, pipeline);
  EXPECT_EQ(AllWords(transcript), Expected(script));

  FakeModels no_detection(options);
  EXPECT_THROW(RunFakes(RenderScript(script, Millis{6000}), no_detection),
               std::invalid_argument);
  pipeline.language = "fr";
  EXPECT_THROW(RunFakes(RenderScript(script, Millis{6000}), models, pipeline),
               std::invalid_argument);
}

TEST(PipelineTest, PromptUsesGlossaryAndOnlyUnsharedContextOfSameLanguage) {
  // Two windows split inside continuous speech, then English after a pause.
  auto script = Speech(0, 75, Millis{500}, "ko");
  const auto english = Speech(100, 10, Millis{40000}, "en");
  script.insert(script.end(), english.begin(), english.end());
  FakeOptions options = OptionsFor(script);
  std::vector<TranscribeRequest> requests;
  options.on_transcribe = [&](const TranscribeRequest& request) {
    requests.push_back({{}, request.language, request.prompt});
  };
  FakeModels models(options);
  PipelineOptions pipeline;
  pipeline.glossary = {"sillok"};
  RunFakes(RenderScript(script, Millis{46000}), models, pipeline);
  ASSERT_GE(requests.size(), 3u);
  EXPECT_EQ(requests[0].prompt, "sillok");
  // The second window continues the first in Korean: context, but never the
  // words both windows hear.
  const std::string& second = requests[1].prompt;
  EXPECT_TRUE(second.starts_with("sillok ko")) << second;
  EXPECT_EQ(requests[1].language, "ko");
  const std::string& last = requests.back().prompt;
  EXPECT_EQ(requests.back().language, "en");
  EXPECT_EQ(last, "sillok");  // previous text is Korean: not used
}

}  // namespace
}  // namespace sillok
