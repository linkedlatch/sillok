#include <gtest/gtest.h>

#include <map>
#include <span>
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

// A checkpoint kept in memory, as the C ABI keeps one in a file.
class MemoryCheckpoint final : public CheckpointSink {
 public:
  void Start(const std::string& key, const std::string& language) override {
    checkpoint = {.key = key, .language = language};
    ++starts;
  }
  void AddGroup(std::span<const Segment> segments) override {
    checkpoint.segments.insert(checkpoint.segments.end(), segments.begin(), segments.end());
    ++checkpoint.groups;
  }

  Checkpoint checkpoint;
  int starts = 0;
};

// `count` groups of five words, 5 s apart, by two speakers.
std::vector<ScriptWord> Groups(int count) {
  std::vector<ScriptWord> script;
  for (int i = 0; i < count; ++i) {
    const auto group = Speech(10 * i, 5, Milliseconds{500 + 5000 * i});
    script.insert(script.end(), group.begin(), group.end());
  }
  for (std::size_t i = 0; i < script.size(); ++i) script[i].speaker = i < 10 ? 0 : 1;
  return script;
}

// What a transcript says, when and by whom.
std::vector<std::string> Describe(const Transcript& transcript) {
  std::vector<std::string> lines;
  for (const Segment& segment : transcript.segments) {
    lines.push_back(std::to_string(segment.start.count()) + "-" +
                    std::to_string(segment.end.count()) + " " + segment.language + " " +
                    std::to_string(segment.speaker.value_or(-1)) + " " + segment.text);
  }
  return lines;
}

TEST(PipelineTest, ContinuesFromACheckpointWithTheResultOfOneRun) {
  const auto script = Groups(4);
  const AudioBuffer audio = RenderScript(script, Milliseconds{21000});
  PipelineOptions options;
  options.diarize = true;
  const auto quiet = [](Stage, double) {};
  FakeModels whole_models(OptionsFor(script));
  const Transcript whole = RunPipeline(audio, options, whole_models, quiet, {});

  // Stopped while transcribing the third group.
  FakeOptions stopping = OptionsFor(script);
  StopSource source;
  int calls = 0;
  stopping.on_transcribe = [&](const TranscribeRequest&) {
    if (++calls == 3) source.RequestStop();
  };
  FakeModels first(stopping);
  MemoryCheckpoint saved;
  EXPECT_THROW(RunPipeline(audio, options, first, quiet, source.Token(), {}, nullptr, &saved),
               Cancelled);
  EXPECT_EQ(saved.checkpoint.groups, 2u);
  EXPECT_EQ(saved.starts, 1);

  // Continued: only the last two groups are transcribed, the first two are
  // previewed at once, and the transcript is the one of a single run.
  FakeModels second(OptionsFor(script));
  std::vector<std::size_t> previews;
  const Checkpoint resume = saved.checkpoint;
  const Transcript continued = RunPipeline(
      audio, options, second, quiet, {},
      [&](std::span<const Segment> segments) { previews.push_back(segments.size()); }, &resume,
      &saved);
  EXPECT_EQ(second.transcribe_calls(), 2);
  ASSERT_FALSE(previews.empty());
  EXPECT_EQ(previews.front(), resume.segments.size());
  EXPECT_EQ(Describe(continued), Describe(whole));
  EXPECT_EQ(saved.starts, 1);  // the checkpoint went on, not anew
  EXPECT_EQ(saved.checkpoint.groups, 4u);

  // A checkpoint of every group leaves only diarization.
  FakeModels third(OptionsFor(script));
  const Checkpoint done = saved.checkpoint;
  EXPECT_EQ(Describe(RunPipeline(audio, options, third, quiet, {}, {}, &done, &saved)),
            Describe(whole));
  EXPECT_EQ(third.transcribe_calls(), 0);
}

TEST(PipelineTest, StartsAgainFromACheckpointOfOtherInput) {
  const auto script = Groups(2);
  const AudioBuffer audio = RenderScript(script, Milliseconds{11000});
  const auto quiet = [](Stage, double) {};
  MemoryCheckpoint saved;
  FakeModels models(OptionsFor(script));
  RunPipeline(audio, {}, models, quiet, {}, {}, nullptr, &saved);
  const Checkpoint resume = saved.checkpoint;
  ASSERT_EQ(resume.groups, 2u);

  // Other options.
  PipelineOptions glossary;
  glossary.glossary = {"sillok"};
  MemoryCheckpoint other;
  FakeModels again(OptionsFor(script));
  RunPipeline(audio, glossary, again, quiet, {}, {}, &resume, &other);
  EXPECT_EQ(again.transcribe_calls(), 2);
  EXPECT_EQ(other.starts, 1);

  // Another recording.
  const auto longer = Groups(3);
  FakeModels third(OptionsFor(longer));
  MemoryCheckpoint fresh;
  RunPipeline(RenderScript(longer, Milliseconds{16000}), {}, third, quiet, {}, {}, &resume,
              &fresh);
  EXPECT_EQ(third.transcribe_calls(), 3);
  EXPECT_EQ(fresh.starts, 1);
}

}  // namespace
}  // namespace sillok
