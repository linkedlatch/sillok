#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
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
      {}, [&](const Preview& preview) {
        if (!preview.draft) previews.insert(previews.end(), preview.segments.size(), stage);
      });

  EXPECT_EQ(AllWords(transcript), Expected(script));
  // The aligner's times, back in the recording.
  EXPECT_EQ(transcript.segments.front().words.front().start, Milliseconds{1000});
  EXPECT_EQ(transcript.segments.back().words.back().end, Milliseconds{9900});
  EXPECT_EQ(transcript.segments.back().speaker, 1);
  EXPECT_EQ(stages, (std::vector<Stage>{Stage::kAnalyze, Stage::kTranscribe, Stage::kAlign,
                                        Stage::kDiarize}));
  // Previews with times come from the aligner.
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

// Previews by chunk, in the order they came.
struct Shown {
  std::size_t chunk = 0;
  bool draft = false;
  Stage stage = Stage::kAnalyze;
  std::vector<Segment> segments;
};

TEST(PipelineTest, PreviewsWhatWasHeardBeforeTheAlignerTimesIt) {
  const auto script = Speech(0, 150, Milliseconds{500});  // a minute: several chunks
  FakeOptions fakes = OptionsFor(script);
  fakes.separate_alignment = true;
  FakeModels models(fakes);
  Stage stage = Stage::kAnalyze;
  std::vector<Shown> shown;
  const Transcript transcript = RunPipeline(
      RenderScript(script, Milliseconds{61000}), {}, models, [&](Stage now, double) { stage = now; },
      {}, [&](const Preview& preview) {
        shown.push_back({.chunk = preview.chunk,
                         .draft = preview.draft,
                         .stage = stage,
                         .segments = {preview.segments.begin(), preview.segments.end()}});
      });

  std::vector<std::size_t> drafts;
  std::vector<std::size_t> timed;
  std::string drafted_text;
  std::vector<std::string> timed_words;
  for (const Shown& preview : shown) {
    if (preview.draft) {
      // One segment over the chunk, with its text and no words, while transcribing.
      EXPECT_EQ(preview.stage, Stage::kTranscribe);
      ASSERT_EQ(preview.segments.size(), 1u);
      EXPECT_TRUE(preview.segments[0].words.empty());
      EXPECT_LT(preview.segments[0].start, preview.segments[0].end);
      drafts.push_back(preview.chunk);
      drafted_text += (drafted_text.empty() ? "" : " ") + preview.segments[0].text;
    } else {
      // Each chunk's timed segments at once, from the aligner.
      EXPECT_EQ(preview.stage, Stage::kAlign);
      timed.push_back(preview.chunk);
      for (const Segment& segment : preview.segments) {
        for (const Word& word : segment.words) timed_words.push_back(word.text);
      }
    }
  }
  EXPECT_GT(drafts.size(), 1u);
  EXPECT_EQ(timed, drafts);
  for (std::size_t i = 1; i < drafts.size(); ++i) EXPECT_GT(drafts[i], drafts[i - 1]);
  EXPECT_EQ(timed_words, Expected(script));
  std::string all;
  for (const std::string& word : Expected(script)) all += (all.empty() ? "" : " ") + word;
  EXPECT_EQ(drafted_text, all);
  EXPECT_EQ(AllWords(transcript), Expected(script));
}

TEST(PipelineTest, PreviewsTimedWordsByChunkWhileTranscribing) {
  const auto script = Speech(0, 150, Milliseconds{500});
  FakeModels models(OptionsFor(script));
  std::vector<std::size_t> chunks;
  RunPipeline(RenderScript(script, Milliseconds{61000}), {}, models, [](Stage, double) {}, {},
              [&](const Preview& preview) {
                EXPECT_FALSE(preview.draft);
                chunks.push_back(preview.chunk);
              });
  ASSERT_FALSE(chunks.empty());
  EXPECT_EQ(chunks.front(), 0u);
  EXPECT_GT(chunks.back(), 0u);
  for (std::size_t i = 1; i < chunks.size(); ++i) EXPECT_GE(chunks[i], chunks[i - 1]);
}

// Keeps a run's checkpoint in memory.
class MemorySink : public CheckpointSink {
 public:
  void Start(const std::string& key, Milliseconds max_chunk) override {
    saved = {};
    saved.key = key;
    saved.max_chunk = max_chunk;
    ++starts;
  }
  void AddChunk(Stage stage, std::size_t chunk, const ChunkResult& result) override {
    (stage == Stage::kAlign ? saved.aligned : saved.transcribed)[chunk] = result;
  }
  void AddModel(const std::string& stage, const ModelInfo& info) override {
    saved.models[stage] = info;
  }

  Checkpoint saved;
  int starts = 0;
};

// What a transcript says, for comparing runs.
struct Said {
  std::vector<std::string> words;
  std::vector<long long> starts;
  std::vector<std::optional<int>> speakers;
  bool operator==(const Said&) const = default;
};

Said SaidIn(const Transcript& transcript) {
  Said said;
  for (const Segment& segment : transcript.segments) {
    said.speakers.push_back(segment.speaker);
    for (const Word& word : segment.words) {
      said.words.push_back(word.text);
      said.starts.push_back(word.start.count());
    }
  }
  return said;
}

std::vector<ScriptWord> TwoSpeakers() {
  auto script = Speech(0, 75, Milliseconds{500});
  auto second = Speech(75, 75, Milliseconds{31000});
  for (ScriptWord& word : second) word.speaker = 1;
  script.insert(script.end(), second.begin(), second.end());
  return script;
}

TEST(PipelineTest, GoesOnFromACheckpointAsOneRunWould) {
  const auto script = TwoSpeakers();
  const AudioBuffer audio = RenderScript(script, Milliseconds{62000});
  PipelineOptions options;
  options.diarize = true;
  FakeModels whole(OptionsFor(script));
  const Transcript expected = RunPipeline(audio, options, whole, [](Stage, double) {}, {});
  const int chunks = whole.transcribe_calls();
  ASSERT_GT(chunks, 1);

  // Stopped while transcribing the second chunk: the first is kept.
  FakeOptions stopping = OptionsFor(script);
  StopSource source;
  int calls = 0;
  stopping.on_transcribe = [&](const TranscribeRequest&) {
    if (++calls == 2) source.RequestStop();
  };
  FakeModels first(stopping);
  MemorySink sink;
  EXPECT_THROW(RunPipeline(audio, options, first, [](Stage, double) {}, source.Token(), {},
                           nullptr, &sink),
               Cancelled);
  EXPECT_EQ(sink.starts, 1);
  ASSERT_EQ(sink.saved.transcribed.size(), 1u);
  EXPECT_EQ(sink.saved.max_chunk, Milliseconds{30000});
  EXPECT_EQ(sink.saved.models.at("transcription").name, "fake-transcriber");

  FakeModels second(OptionsFor(script));
  std::vector<std::size_t> previewed;
  const Checkpoint resume = sink.saved;
  const Transcript continued = RunPipeline(
      audio, options, second, [](Stage, double) {}, {},
      [&](const Preview& preview) { previewed.push_back(preview.chunk); }, &resume, &sink);
  EXPECT_EQ(SaidIn(continued), SaidIn(expected));
  EXPECT_EQ(second.transcribe_calls(), chunks - 1);
  EXPECT_EQ(sink.starts, 1);  // went on in the same checkpoint
  EXPECT_EQ(sink.saved.transcribed.size(), static_cast<std::size_t>(chunks));
  // The kept chunk is previewed first.
  ASSERT_FALSE(previewed.empty());
  EXPECT_EQ(previewed.front(), 0u);
  EXPECT_EQ(continued.models.at("transcription").name, "fake-transcriber");
  EXPECT_EQ(continued.models.count("diarization"), 1u);
}

TEST(PipelineTest, LoadsNeitherModelWhenTheCheckpointHasEveryChunk) {
  const auto script = TwoSpeakers();
  const AudioBuffer audio = RenderScript(script, Milliseconds{62000});
  FakeOptions fakes = OptionsFor(script);
  fakes.separate_alignment = true;
  PipelineOptions options;
  options.diarize = true;
  FakeModels first(fakes);
  MemorySink sink;
  const Transcript expected =
      RunPipeline(audio, options, first, [](Stage, double) {}, {}, {}, nullptr, &sink);
  EXPECT_FALSE(sink.saved.aligned.empty());

  // As after a run that failed in diarization.
  FakeModels second(fakes);
  std::vector<bool> drafts;
  const Checkpoint resume = sink.saved;
  const Transcript continued = RunPipeline(
      audio, options, second, [](Stage, double) {}, {},
      [&](const Preview& preview) { drafts.push_back(preview.draft); }, &resume, &sink);
  EXPECT_EQ(SaidIn(continued), SaidIn(expected));
  EXPECT_EQ(second.transcribers_opened(), 0);
  EXPECT_EQ(second.aligners_opened(), 0);
  EXPECT_EQ(continued.models.at("transcription").name, "fake-transcriber");
  EXPECT_EQ(continued.models.at("alignment").name, "fake-aligner");
  // Drafts first, then their timed segments.
  ASSERT_FALSE(drafts.empty());
  EXPECT_TRUE(drafts.front());
  EXPECT_FALSE(drafts.back());
}

TEST(PipelineTest, GoesOnAligningFromACheckpoint) {
  const auto script = TwoSpeakers();
  const AudioBuffer audio = RenderScript(script, Milliseconds{62000});
  FakeOptions fakes = OptionsFor(script);
  fakes.separate_alignment = true;
  FakeModels whole(fakes);
  const Transcript expected = RunPipeline(audio, {}, whole, [](Stage, double) {}, {});

  // Stopped once the first chunk is aligned.
  FakeModels first(fakes);
  MemorySink sink;
  StopSource source;
  EXPECT_THROW(RunPipeline(audio, {}, first,
                           [&](Stage stage, double fraction) {
                             if (stage == Stage::kAlign && fraction > 0) source.RequestStop();
                           },
                           source.Token(), {}, nullptr, &sink),
               Cancelled);
  EXPECT_EQ(sink.saved.aligned.size(), 1u);
  EXPECT_GT(sink.saved.transcribed.size(), 1u);

  FakeModels second(fakes);
  const Checkpoint resume = sink.saved;
  const Transcript continued =
      RunPipeline(audio, {}, second, [](Stage, double) {}, {}, {}, &resume, &sink);
  EXPECT_EQ(SaidIn(continued), SaidIn(expected));
  EXPECT_EQ(second.transcribers_opened(), 0);
  EXPECT_EQ(second.aligners_opened(), 1);
}

TEST(PipelineTest, StartsAnewFromACheckpointOfOtherOptions) {
  const auto script = TwoSpeakers();
  const AudioBuffer audio = RenderScript(script, Milliseconds{62000});
  FakeModels first(OptionsFor(script));
  MemorySink sink;
  RunPipeline(audio, {}, first, [](Stage, double) {}, {}, {}, nullptr, &sink);
  const Checkpoint resume = sink.saved;

  PipelineOptions english;
  english.language = "en";
  FakeModels second(OptionsFor(script));
  RunPipeline(audio, english, second, [](Stage, double) {}, {}, {}, &resume, &sink);
  EXPECT_EQ(second.transcribe_calls(), first.transcribe_calls());
  EXPECT_EQ(sink.starts, 2);
  EXPECT_NE(sink.saved.key, resume.key);
}

// Keeps a run's detected speech in memory.
class MemorySpeech : public SpeechStore {
 public:
  std::optional<DetectedSpeech> Load() override { return saved; }
  void Save(const DetectedSpeech& speech) override {
    saved = speech;
    ++saves;
  }

  std::optional<DetectedSpeech> saved;
  int saves = 0;
};

TEST(PipelineTest, DetectsSpeechOnceForEveryRunOfARecording) {
  const auto script = TwoSpeakers();
  const AudioBuffer audio = RenderScript(script, Milliseconds{62000});
  FakeOptions fakes = OptionsFor(script);
  int detections = 0;
  fakes.on_detect = [&] { ++detections; };
  MemorySpeech speech;
  FakeModels first(fakes);
  const Transcript expected =
      RunPipeline(audio, {}, first, [](Stage, double) {}, {}, {}, nullptr, nullptr, &speech);
  EXPECT_GT(detections, 0);
  EXPECT_EQ(speech.saves, 1);

  PipelineOptions english;
  english.language = "en";
  std::vector<std::pair<Stage, double>> analyzed;
  FakeModels second(fakes);
  detections = 0;
  const Transcript again = RunPipeline(
      audio, english, second,
      [&](Stage stage, double fraction) {
        if (stage == Stage::kAnalyze) analyzed.emplace_back(stage, fraction);
      },
      {}, {}, nullptr, nullptr, &speech);
  EXPECT_EQ(detections, 0);
  EXPECT_EQ(speech.saves, 1);
  EXPECT_EQ(SaidIn(again), SaidIn(expected));
  EXPECT_EQ(again.models.at("voice_detection").name, "fake-vad");
  EXPECT_EQ(analyzed, (std::vector<std::pair<Stage, double>>{{Stage::kAnalyze, 0.0},
                                                             {Stage::kAnalyze, 1.0}}));

  const AudioBuffer other = RenderScript(Speech(0, 10, Milliseconds{500}), Milliseconds{5000});
  FakeModels third(fakes);
  RunPipeline(other, {}, third, [](Stage, double) {}, {}, {}, nullptr, nullptr, &speech);
  EXPECT_GT(detections, 0);
  EXPECT_EQ(speech.saves, 2);
}

}  // namespace
}  // namespace sillok
