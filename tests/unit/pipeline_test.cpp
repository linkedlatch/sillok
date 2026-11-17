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

TEST(PipelineTest, FillsEachRequestUpToTheModelsLimit) {
  // A minute without a pause is cut into requests of at most the model's
  // 30 s, at pauses between words; what is left goes with the speech after
  // the long pause.
  auto script = Speech(0, 150, Milliseconds{500});
  const auto after = Speech(200, 10, Milliseconds{70000});
  script.insert(script.end(), after.begin(), after.end());
  FakeOptions options = OptionsFor(script);
  std::vector<std::size_t> heard;
  options.on_transcribe = [&](const TranscribeRequest& request) {
    heard.push_back(request.pcm.size());
  };
  FakeModels models(options);
  const Transcript transcript = RunFakes(RenderScript(script, Milliseconds{75000}), models);
  ASSERT_EQ(heard.size(), 3u);
  // Cut pieces are at least half the limit (and the fake checks the limit).
  EXPECT_GT(heard[0], static_cast<std::size_t>(ToSamples(Milliseconds{15000})));
  EXPECT_GT(heard[1], static_cast<std::size_t>(ToSamples(Milliseconds{15000})));
  EXPECT_EQ(AllWords(transcript), Expected(script));
}

TEST(PipelineTest, HearsSpeechAcrossALongPauseTogether) {
  // 20 s apart, but both fit in one request.
  auto script = Speech(0, 5, Milliseconds{500});
  const auto after = Speech(10, 5, Milliseconds{22500});
  script.insert(script.end(), after.begin(), after.end());
  FakeModels models(OptionsFor(script));
  const Transcript transcript = RunFakes(RenderScript(script, Milliseconds{25000}), models);
  EXPECT_EQ(models.transcribe_calls(), 1);
  EXPECT_EQ(AllWords(transcript), Expected(script));
  EXPECT_NEAR(static_cast<double>(transcript.segments.back().words.back().start.count()),
              22500.0 + 4 * 400.0, 40.0);
}

TEST(PipelineTest, TakesTheShorterOfItsOwnAndTheModelsLimit) {
  const auto script = Speech(0, 50, Milliseconds{500});
  FakeOptions options = OptionsFor(script);
  options.max_audio = Milliseconds{8000};
  FakeModels models(options);
  RunFakes(RenderScript(script, Milliseconds{21000}), models);
  EXPECT_EQ(models.transcribe_calls(), 3);  // 20 s, in pieces of 4 to 8 s

  FakeModels longer(OptionsFor(script));
  PipelineOptions pipeline;
  pipeline.segmenter.max_chunk = Milliseconds{12000};
  RunFakes(RenderScript(script, Milliseconds{21000}), longer, pipeline);
  EXPECT_EQ(longer.transcribe_calls(), 2);
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
  std::vector<double> transcribing;
  const Transcript transcript =
      RunFakes(RenderScript({}, Milliseconds{10000}), models, {}, [&](Stage stage, double fraction) {
        if (stage == Stage::kTranscribe) transcribing.push_back(fraction);
      });
  EXPECT_TRUE(transcript.segments.empty());
  EXPECT_EQ(models.transcribe_calls(), 0);
  EXPECT_EQ(models.transcribers_opened(), 0);
  // The stage still starts with 0, as every stage does.
  EXPECT_EQ(transcribing, (std::vector<double>{0.0, 1.0}));
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

TEST(PipelineTest, AlignsTextWithoutTimesInAStageOfItsOwn) {
  auto script = Speech(0, 10, Milliseconds{1000});
  auto second = Speech(10, 10, Milliseconds{6000});
  for (ScriptWord& word : second) word.speaker = 1;
  script.insert(script.end(), second.begin(), second.end());
  FakeOptions fakes = OptionsFor(script);
  fakes.separate_alignment = true;
  FakeModels models(fakes);
  PipelineOptions options;
  options.diarize = true;
  Stage stage = Stage::kAnalyze;
  std::vector<Stage> stages;
  std::vector<Stage> previews;
  const Transcript transcript = RunPipeline(
      RenderScript(script, Milliseconds{12000}), options, models,
      [&](Stage now, double) {
        stage = now;
        if (stages.empty() || stages.back() != now) stages.push_back(now);
      },
      {}, [&](std::span<const Segment> segments) { previews.insert(previews.end(), segments.size(), stage); });

  EXPECT_EQ(AllWords(transcript), Expected(script));
  // The aligner's times, back in the recording.
  EXPECT_EQ(transcript.segments.front().words.front().start, Milliseconds{1000});
  EXPECT_EQ(transcript.segments.back().words.back().end, Milliseconds{9900});
  EXPECT_EQ(transcript.segments.back().speaker, 1);
  EXPECT_EQ(stages, (std::vector<Stage>{Stage::kAnalyze, Stage::kTranscribe, Stage::kAlign,
                                        Stage::kDiarize}));
  // Previews come with times, so from the aligner.
  ASSERT_FALSE(previews.empty());
  for (const Stage at : previews) EXPECT_EQ(at, Stage::kAlign);
  EXPECT_FALSE(models.live().aligner_with_transcriber);
  EXPECT_EQ(models.live().aligners, 0);
  EXPECT_EQ(transcript.models.count("alignment"), 1u);
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
  // Stopped as the stage started, before its model loaded.
  EXPECT_EQ(models.transcribers_opened(), 0);
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

TEST(PipelineTest, DetectsTheLanguageOfEachRequest) {
  // Korean, then English: each request in its own language, told from its
  // speech, so the English word in the Korean request stays Korean.
  auto script = Speech(0, 10, Milliseconds{500}, "ko");
  script.push_back({.id = 50, .start = Milliseconds{4500}, .end = Milliseconds{4800},
                    .language = "en", .speaker = 0});
  const auto english = Speech(200, 10, Milliseconds{10000}, "en");
  script.insert(script.end(), english.begin(), english.end());
  FakeOptions options = OptionsFor(script);
  std::vector<std::string> requested;
  options.on_transcribe = [&](const TranscribeRequest& request) {
    requested.push_back(request.language);
  };
  FakeModels models(options);
  PipelineOptions pipeline;
  pipeline.segmenter.max_chunk = Milliseconds{5000};  // one request each
  const Transcript transcript =
      RunFakes(RenderScript(script, Milliseconds{15000}), models, pipeline);
  EXPECT_EQ(requested, (std::vector<std::string>{"", ""}));
  std::vector<std::string> words;
  for (const Segment& segment : transcript.segments) {
    for (const Word& word : segment.words) words.push_back(segment.language + ":" + word.text);
  }
  std::vector<std::string> expected;
  for (const ScriptWord& word : script) {
    const std::string language = word.start < Milliseconds{10000} ? "ko" : "en";
    expected.push_back(language + ":" + WordText(word.id, language));
  }
  EXPECT_EQ(words, expected);
}

TEST(PipelineTest, FixedLanguageIsGivenToEveryRequest) {
  auto script = Speech(0, 10, Milliseconds{500}, "ko");
  const auto english = Speech(200, 10, Milliseconds{10000}, "en");
  script.insert(script.end(), english.begin(), english.end());
  FakeOptions options = OptionsFor(script);
  std::vector<std::string> requested;
  options.on_transcribe = [&](const TranscribeRequest& request) {
    requested.push_back(request.language);
  };
  FakeModels models(options);
  PipelineOptions pipeline;
  pipeline.segmenter.max_chunk = Milliseconds{5000};  // one request each
  pipeline.language = "ko";
  const Transcript transcript =
      RunFakes(RenderScript(script, Milliseconds{15000}), models, pipeline);
  EXPECT_EQ(requested, (std::vector<std::string>{"ko", "ko"}));
  for (const Segment& segment : transcript.segments) EXPECT_EQ(segment.language, "ko");

  pipeline.language = "fr";  // not one of the model's languages
  EXPECT_THROW(RunFakes(RenderScript(script, Milliseconds{15000}), models, pipeline),
               std::invalid_argument);
}

TEST(PipelineTest, ModelsGetTheGlossary) {
  auto script = Speech(0, 5, Milliseconds{500});
  const auto after = Speech(10, 5, Milliseconds{10000});
  script.insert(script.end(), after.begin(), after.end());
  FakeOptions options = OptionsFor(script);
  std::vector<std::vector<std::string>> glossaries;
  options.on_transcribe = [&](const TranscribeRequest& request) {
    glossaries.emplace_back(request.glossary.begin(), request.glossary.end());
  };
  FakeModels models(options);
  PipelineOptions pipeline;
  pipeline.segmenter.max_chunk = Milliseconds{3000};  // one request each
  pipeline.glossary = {"sillok", "Whisper"};
  RunFakes(RenderScript(script, Milliseconds{13000}), models, pipeline);
  const std::vector<std::string> glossary = {"sillok", "Whisper"};
  EXPECT_EQ(glossaries, (std::vector<std::vector<std::string>>{glossary, glossary}));
}

TEST(PipelineTest, RefusesAGlossaryTheModelHasNoRoomFor) {
  const auto script = Speech(0, 5, Milliseconds{500});
  FakeOptions options = OptionsFor(script);
  options.max_audio = Milliseconds{20000};
  options.glossary_room = 15;  // "sillok, Whisper"
  FakeModels models(options);
  PipelineOptions pipeline;
  pipeline.glossary = {"sillok", "Whisper"};
  RunFakes(RenderScript(script, Milliseconds{3000}), models, pipeline);
  pipeline.segmenter.max_chunk = Milliseconds{5000};
  RunFakes(RenderScript(script, Milliseconds{3000}), models, pipeline);
  // Once a run, for the longest request: the model's limit or the pipeline's.
  EXPECT_EQ(models.glossary_checks(),
            (std::vector<Milliseconds>{Milliseconds{20000}, Milliseconds{5000}}));
  EXPECT_EQ(models.transcribe_calls(), 2);

  pipeline.glossary.push_back("x");
  try {
    RunFakes(RenderScript(script, Milliseconds{3000}), models, pipeline);
    ADD_FAILURE() << "the glossary was taken";
  } catch (const std::invalid_argument& error) {
    EXPECT_STREQ(error.what(), "glossary is 18 tokens; this model takes at most 15");
  }
  EXPECT_EQ(models.transcribe_calls(), 2);  // refused before transcribing
}

}  // namespace
}  // namespace sillok
