#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/files.hpp"
#include "fakes/fake_backend.hpp"
#include "sillok/sillok.h"

namespace sillok {
namespace {

using Json = nlohmann::json;
using testing::FakeBackendOptions;
using testing::FakeOptions;
using testing::RenderScript;
using testing::ScriptWord;

// Takes ownership of a string returned by sillok.
std::string Take(char* text) {
  std::string copy = text ? text : "";
  sillok_free_string(text);
  return copy;
}

std::vector<float> ScriptPcm(int words, int first_id = 0) {
  std::vector<ScriptWord> script;
  for (int i = 0; i < words; ++i) {
    const Milliseconds start{500 + 400 * i};
    script.push_back({first_id + i, start, start + Milliseconds{300}, "en", i < words / 2 ? 0 : 1});
  }
  const AudioBuffer audio =
      RenderScript(script, Milliseconds{1000 + 400 * words});
  return audio.Read({0, audio.size()});
}

// The committed media files (tests/data/media).
std::filesystem::path MediaFile(const char* name) {
  return std::filesystem::path(SILLOK_TEST_MEDIA_DIR) / name;
}

// Writes `seconds` of a 440 Hz tone as 16-bit PCM WAV.
void WriteWav(const std::filesystem::path& path, int rate, int channels, double seconds) {
  const auto frames = static_cast<std::uint32_t>(rate * seconds);
  const auto bytes = frames * static_cast<std::uint32_t>(channels) * 2;
  std::ofstream out(path, std::ios::binary);
  const auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
  const auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
  out.write("RIFF", 4);
  u32(36 + bytes);
  out.write("WAVEfmt ", 8);
  u32(16);
  u16(1);
  u16(static_cast<std::uint16_t>(channels));
  u32(static_cast<std::uint32_t>(rate));
  u32(static_cast<std::uint32_t>(rate * channels * 2));
  u16(static_cast<std::uint16_t>(channels * 2));
  u16(16);
  out.write("data", 4);
  u32(bytes);
  for (std::uint32_t i = 0; i < frames; ++i) {
    const auto value = static_cast<std::int16_t>(
        8000 * std::sin(2 * 3.14159265 * 440 * static_cast<double>(i) / rate));
    for (int c = 0; c < channels; ++c) u16(static_cast<std::uint16_t>(value));
  }
}

class CapiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // One directory per test: ctest runs the tests in parallel processes.
    models_dir_ = std::filesystem::temp_directory_path() /
                  PathFromUtf8(std::string("sillok 모델 capi ") +
                               ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::create_directories(models_dir_);
    FakeBackendOptions() = FakeOptions{};
  }
  void TearDown() override {
    for (sillok_job* job : jobs_) sillok_job_destroy(job);
    for (sillok_engine* engine : engines_) sillok_engine_destroy(engine);
    std::filesystem::remove_all(models_dir_);
  }

  sillok_engine* Engine() {
    const Json config = {{"models_dir", PathToUtf8(models_dir_)}, {"device", "gpu"}};
    sillok_engine* engine = nullptr;
    EXPECT_EQ(sillok_engine_create(config.dump().c_str(), &engine), SILLOK_OK)
        << sillok_last_error();
    engines_.push_back(engine);
    return engine;
  }

  sillok_job* Job(sillok_engine* engine, const char* options = nullptr) {
    sillok_job* job = nullptr;
    EXPECT_EQ(sillok_job_create(engine, options, &job), SILLOK_OK) << sillok_last_error();
    jobs_.push_back(job);
    return job;
  }

  static void Push(sillok_job* job, const std::vector<float>& pcm) {
    ASSERT_EQ(sillok_job_push_audio(job, pcm.data(), pcm.size(), 16000, 1), SILLOK_OK)
        << sillok_last_error();
  }

  // Runs `job` and returns the length of its recording, as the transcript
  // reports it.
  static long long DurationMs(sillok_job* job) {
    char* out = nullptr;
    EXPECT_EQ(sillok_job_run(job, nullptr, nullptr, &out), SILLOK_OK) << sillok_last_error();
    return out != nullptr ? Json::parse(Take(out))["duration_ms"].get<long long>() : -1;
  }

  std::filesystem::path models_dir_;
  std::vector<sillok_engine*> engines_;
  std::vector<sillok_job*> jobs_;
};

TEST_F(CapiTest, TranscribesAndReportsProgress) {
  sillok_job* job = Job(Engine(), R"({"language": "en", "glossary": ["sillok"]})");
  Push(job, ScriptPcm(10));
  std::vector<sillok_stage> stages;
  const auto progress = [](void* user, sillok_stage stage, double) {
    static_cast<std::vector<sillok_stage>*>(user)->push_back(stage);
  };
  char* out = nullptr;
  ASSERT_EQ(sillok_job_run(job, progress, &stages, &out), SILLOK_OK) << sillok_last_error();
  const Json transcript = Json::parse(Take(out));
  EXPECT_EQ(transcript["version"], 1);
  EXPECT_EQ(transcript["duration_ms"], 5000);
  std::string text;
  for (const Json& segment : transcript["segments"]) {
    text += segment["text"].get<std::string>() + " ";
    EXPECT_FALSE(segment.contains("speaker"));
    EXPECT_GE(segment["end_ms"].get<long long>(), segment["start_ms"].get<long long>());
  }
  EXPECT_EQ(text, "en0 en1 en2 en3 en4 en5 en6 en7 en8 en9 ");
  ASSERT_FALSE(stages.empty());
  EXPECT_EQ(stages.front(), SILLOK_STAGE_ANALYZE);
  EXPECT_EQ(stages.back(), SILLOK_STAGE_TRANSCRIBE);
  EXPECT_EQ(transcript["models"]["transcription"]["name"], "fake-transcriber");
}

TEST_F(CapiTest, PreviewsSegmentsWhileTranscribing) {
  sillok_job* job = Job(Engine(), R"({"language": "en"})");
  Push(job, ScriptPcm(150));  // a minute: several windows
  struct Previews {
    int calls = 0;
    std::vector<std::string> texts;
  } previews;
  ASSERT_EQ(sillok_job_on_segments(job, [](void* user, const char* json) {
    auto* self = static_cast<Previews*>(user);
    ++self->calls;
    for (const Json& segment : Json::parse(json)) {
      EXPECT_FALSE(segment.contains("speaker"));
      self->texts.push_back(segment["text"]);
    }
  }, &previews), SILLOK_OK);
  char* out = nullptr;
  ASSERT_EQ(sillok_job_run(job, nullptr, nullptr, &out), SILLOK_OK) << sillok_last_error();
  const Json transcript = Json::parse(Take(out));
  std::vector<std::string> final_texts;
  for (const Json& segment : transcript["segments"]) final_texts.push_back(segment["text"]);
  EXPECT_GT(previews.calls, 1);
  EXPECT_EQ(previews.texts, final_texts);
  EXPECT_EQ(sillok_job_on_segments(job, nullptr, nullptr), SILLOK_INVALID_ARGUMENT);  // after the run
}

TEST_F(CapiTest, DiarizesAndListsSpeakers) {
  FakeBackendOptions().speakers = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0},
                                   {5, 1}, {6, 1}, {7, 1}, {8, 1}, {9, 1}};
  sillok_job* job = Job(Engine(), R"({"diarize": true})");
  Push(job, ScriptPcm(10));
  char* out = nullptr;
  ASSERT_EQ(sillok_job_run(job, nullptr, nullptr, &out), SILLOK_OK) << sillok_last_error();
  const Json transcript = Json::parse(Take(out));
  EXPECT_EQ(transcript["speakers"],
            Json::parse(R"([{"id": 0, "name": null}, {"id": 1, "name": null}])"));
  EXPECT_EQ(transcript["segments"].front()["speaker"], 0);
  EXPECT_EQ(transcript["segments"].back()["speaker"], 1);
}

TEST_F(CapiTest, AcceptsAnyRateAndChannelCount) {
  sillok_job* job = Job(Engine());
  const std::vector<float> stereo(48000 * 2, 0.0f);  // 1 s at 48 kHz
  ASSERT_EQ(sillok_job_push_audio(job, stereo.data(), 48000, 48000, 2), SILLOK_OK);
  EXPECT_EQ(sillok_job_push_audio(job, stereo.data(), 100, 44100, 2), SILLOK_INVALID_ARGUMENT);
  EXPECT_NE(std::string(sillok_last_error()), "");
  char* out = nullptr;
  ASSERT_EQ(sillok_job_run(job, nullptr, nullptr, &out), SILLOK_OK);
  EXPECT_EQ(Json::parse(Take(out))["duration_ms"], 1000);
}

TEST_F(CapiTest, TakesNoAudioAfterTheRecordingEnds) {
  sillok_job* job = Job(Engine());
  // 1 s at 44.1 kHz stereo, pushed in decoder-sized blocks.
  const std::vector<float> stereo(44100 * 2, 0.1f);
  for (std::size_t frame = 0; frame < 44100; frame += 1024) {
    const std::size_t frames = std::min<std::size_t>(1024, 44100 - frame);
    ASSERT_EQ(sillok_job_push_audio(job, stereo.data() + frame * 2, frames, 44100, 2), SILLOK_OK);
  }
  ASSERT_EQ(sillok_job_end_audio(job), SILLOK_OK);
  EXPECT_EQ(sillok_job_push_audio(job, stereo.data(), 10, 44100, 2), SILLOK_INVALID_ARGUMENT);
  EXPECT_NE(std::string(sillok_last_error()).find("end_audio"), std::string::npos);
  ASSERT_EQ(sillok_job_end_audio(job), SILLOK_OK);  // again: no effect
  EXPECT_EQ(DurationMs(job), 1000);
}

TEST_F(CapiTest, RejectsBadInput) {
  sillok_engine* engine = nullptr;
  EXPECT_EQ(sillok_engine_create("{", &engine), SILLOK_INVALID_ARGUMENT);
  EXPECT_EQ(sillok_engine_create(R"({"models_dir": "x", "devise": "cpu"})", &engine),
            SILLOK_INVALID_ARGUMENT);
  EXPECT_NE(std::string(sillok_last_error()).find("devise"), std::string::npos);
  EXPECT_EQ(sillok_engine_create(R"({"models_dir": "/no/such/dir"})", &engine),
            SILLOK_MODEL_ERROR);
  const Json bad_temp = {{"models_dir", PathToUtf8(models_dir_)},
                         {"temp_dir", "/no/such/temp/dir"}};
  EXPECT_EQ(sillok_engine_create(bad_temp.dump().c_str(), &engine), SILLOK_INVALID_ARGUMENT);
  const Json bad_npu = {{"models_dir", PathToUtf8(models_dir_)}, {"npu", "yes"}};
  EXPECT_EQ(sillok_engine_create(bad_npu.dump().c_str(), &engine), SILLOK_INVALID_ARGUMENT);
  EXPECT_EQ(engine, nullptr);
  const Json npu = {{"models_dir", PathToUtf8(models_dir_)}, {"device", "cpu"}, {"npu", true}};
  EXPECT_EQ(sillok_engine_create(npu.dump().c_str(), &engine), SILLOK_OK) << sillok_last_error();
  sillok_engine_destroy(engine);
  engine = nullptr;
  sillok_job* job = nullptr;
  EXPECT_EQ(sillok_job_create(Engine(), R"({"language": "fr"})", &job),
            SILLOK_INVALID_ARGUMENT);
  EXPECT_EQ(sillok_job_create(nullptr, nullptr, &job), SILLOK_INVALID_ARGUMENT);
}

TEST_F(CapiTest, JobRunsOnceAndTakesNoAudioAfterwards) {
  sillok_job* job = Job(Engine());
  Push(job, ScriptPcm(3));
  char* out = nullptr;
  ASSERT_EQ(sillok_job_run(job, nullptr, nullptr, &out), SILLOK_OK);
  Take(out);
  EXPECT_EQ(sillok_job_run(job, nullptr, nullptr, &out), SILLOK_INVALID_ARGUMENT);
  const std::vector<float> more(10, 0.0f);
  EXPECT_EQ(sillok_job_push_audio(job, more.data(), more.size(), 16000, 1),
            SILLOK_INVALID_ARGUMENT);
}

TEST_F(CapiTest, CancelBeforeRun) {
  sillok_job* job = Job(Engine());
  Push(job, ScriptPcm(3));
  sillok_job_cancel(job);
  char* out = nullptr;
  EXPECT_EQ(sillok_job_run(job, nullptr, nullptr, &out), SILLOK_CANCELLED);
  EXPECT_EQ(out, nullptr);
}

TEST_F(CapiTest, CancelFromAnotherThreadDuringRun) {
  sillok_job* job = nullptr;
  int calls = 0;
  // The engine copies the fake options, so set them before creating it.
  FakeBackendOptions().on_transcribe = [&](const TranscribeRequest&) {
    ++calls;
    std::thread([&] { sillok_job_cancel(job); }).join();
  };
  job = Job(Engine());
  Push(job, ScriptPcm(10));
  char* out = nullptr;
  EXPECT_EQ(sillok_job_run(job, nullptr, nullptr, &out), SILLOK_CANCELLED);
  EXPECT_EQ(calls, 1);
}

// Speech in four groups of five words, 5 s apart.
std::vector<float> GroupsPcm() {
  std::vector<ScriptWord> script;
  for (int group = 0; group < 4; ++group) {
    for (int i = 0; i < 5; ++i) {
      const Milliseconds start{500 + 5000 * group + 400 * i};
      script.push_back({10 * group + i, start, start + Milliseconds{300}, "en", 0});
    }
  }
  const AudioBuffer audio = RenderScript(script, Milliseconds{21000});
  return audio.Read({0, audio.size()});
}

TEST_F(CapiTest, PausedJobContinuesFromItsCheckpoint) {
  const auto checkpoint = models_dir_ / PathFromUtf8("중간 저장.jsonl");
  const Json options = {{"checkpoint", PathToUtf8(checkpoint)}};
  const std::vector<float> pcm = GroupsPcm();

  // The result of one run, without a checkpoint.
  sillok_job* whole = Job(Engine());
  Push(whole, pcm);
  char* out = nullptr;
  ASSERT_EQ(sillok_job_run(whole, nullptr, nullptr, &out), SILLOK_OK);
  const Json expected = Json::parse(Take(out))["segments"];

  // Paused in the third group: the checkpoint stays. The engine copies the
  // fake options, so they are set before creating it.
  sillok_job* paused = nullptr;
  int calls = 0;
  FakeBackendOptions().on_transcribe = [&](const TranscribeRequest&) {
    if (++calls == 3) sillok_job_cancel(paused);
  };
  paused = Job(Engine(), options.dump().c_str());
  Push(paused, pcm);
  EXPECT_EQ(sillok_job_run(paused, nullptr, nullptr, &out), SILLOK_CANCELLED);
  ASSERT_TRUE(std::filesystem::exists(checkpoint));

  // A crash while writing leaves half a line, which is dropped.
  std::ofstream(checkpoint, std::ios::binary | std::ios::app) << "[{\"start_ms\": 1";

  // Continued: two groups left, the same transcript, and no checkpoint after.
  calls = 0;
  FakeBackendOptions().on_transcribe = [&](const TranscribeRequest&) { ++calls; };
  sillok_job* continued = Job(Engine(), options.dump().c_str());
  Push(continued, pcm);
  ASSERT_EQ(sillok_job_run(continued, nullptr, nullptr, &out), SILLOK_OK) << sillok_last_error();
  EXPECT_EQ(Json::parse(Take(out))["segments"], expected);
  EXPECT_EQ(calls, 2);
  EXPECT_FALSE(std::filesystem::exists(checkpoint));
}

TEST_F(CapiTest, IgnoresACheckpointItCannotRead) {
  const auto checkpoint = models_dir_ / "checkpoint.jsonl";
  std::ofstream(checkpoint, std::ios::binary) << "not a checkpoint\n";
  int calls = 0;
  FakeBackendOptions().on_transcribe = [&](const TranscribeRequest&) { ++calls; };
  const Json options = {{"checkpoint", PathToUtf8(checkpoint)}};
  sillok_job* job = Job(Engine(), options.dump().c_str());
  Push(job, GroupsPcm());
  char* out = nullptr;
  ASSERT_EQ(sillok_job_run(job, nullptr, nullptr, &out), SILLOK_OK) << sillok_last_error();
  Take(out);
  EXPECT_EQ(calls, 4);
  EXPECT_FALSE(std::filesystem::exists(checkpoint));
}

TEST_F(CapiTest, TakesAudioWithItsLoudnessAsItIs) {
  // For a copy whose loudness was evened out already; the samples: AudioInputTest.
  sillok_job* job = Job(Engine(), R"({"even_loudness": false})");
  Push(job, ScriptPcm(3));
  EXPECT_GT(DurationMs(job), 0);
  sillok_job* bad = nullptr;
  EXPECT_EQ(sillok_job_create(Engine(), R"({"even_loudness": "no"})", &bad),
            SILLOK_INVALID_ARGUMENT);
}

TEST_F(CapiTest, EngineRunsOneJobAtATime) {
  sillok_job* second = nullptr;
  sillok_status second_status = SILLOK_OK;
  FakeBackendOptions().on_transcribe = [&](const TranscribeRequest&) {
    char* out = nullptr;
    second_status = sillok_job_run(second, nullptr, nullptr, &out);
  };
  sillok_engine* engine = Engine();
  second = Job(engine);
  Push(second, ScriptPcm(3));
  sillok_job* first = Job(engine);
  Push(first, ScriptPcm(3));
  char* out = nullptr;
  ASSERT_EQ(sillok_job_run(first, nullptr, nullptr, &out), SILLOK_OK);
  Take(out);
  EXPECT_EQ(second_status, SILLOK_BUSY);
}

TEST_F(CapiTest, JobKeepsEngineAlive) {
  sillok_engine* engine = Engine();
  sillok_job* job = Job(engine);
  sillok_engine_destroy(engine);
  engines_.clear();
  Push(job, ScriptPcm(3));
  char* out = nullptr;
  EXPECT_EQ(sillok_job_run(job, nullptr, nullptr, &out), SILLOK_OK);
  Take(out);
}

TEST_F(CapiTest, ExportsEditedDocuments) {
  const std::string edited = R"({
    "version": 1, "duration_ms": 4000,
    "speakers": [{"id": 0, "name": "김교수"}, {"id": 1, "name": null}],
    "segments": [
      {"start_ms": 0, "end_ms": 1200, "text": "안녕하세요", "language": "ko",
       "speaker": 0, "words": []},
      {"start_ms": 1500, "end_ms": 3000, "text": "질문 있습니다", "language": "ko",
       "speaker": 1}]})";
  char* out = nullptr;
  ASSERT_EQ(sillok_export(edited.c_str(), "txt", R"({"speaker_label": "화자 {n}"})", &out),
            SILLOK_OK) << sillok_last_error();
  EXPECT_EQ(Take(out), "김교수: 안녕하세요\n화자 2: 질문 있습니다\n");
  EXPECT_EQ(sillok_export(edited.c_str(), "srt", nullptr, &out), SILLOK_INVALID_ARGUMENT);
  ASSERT_EQ(sillok_export(edited.c_str(), "json", nullptr, &out), SILLOK_OK);
  const Json round_trip = Json::parse(Take(out));
  EXPECT_EQ(round_trip["speakers"][0]["name"], "김교수");
  EXPECT_EQ(round_trip["segments"][1]["text"], "질문 있습니다");
  EXPECT_EQ(sillok_export(edited.c_str(), "doc", nullptr, &out), SILLOK_INVALID_ARGUMENT);
  EXPECT_EQ(sillok_export("[]", "txt", nullptr, &out), SILLOK_INVALID_ARGUMENT);
  EXPECT_EQ(sillok_export(R"({"version": 1, "segments": [{"start_ms": -5}]})", "txt",
                          nullptr, &out),
            SILLOK_INVALID_ARGUMENT);
}

TEST_F(CapiTest, ChecksModelFiles) {
  char* out = nullptr;
  ASSERT_EQ(sillok_model_catalog(&out), SILLOK_OK);
  const Json catalog = Json::parse(Take(out));
  ASSERT_EQ(catalog.size(), 2u);
  EXPECT_EQ(catalog[0]["id"], "fake-transcriber");
  EXPECT_EQ(catalog[0]["role"], "transcription");
  EXPECT_EQ(catalog[0]["family"], "fake");
  EXPECT_EQ(catalog[0]["quantization"], "q5_0");
  EXPECT_TRUE(catalog[0]["recommended"].get<bool>());
  EXPECT_FALSE(catalog[1]["recommended"].get<bool>());
  EXPECT_EQ(catalog[0]["languages"], Json::array({"en", "ko"}));
  ASSERT_EQ(catalog[0]["files"].size(), 2u);
  EXPECT_EQ(catalog[0]["files"][1]["zip"], true);

  using Statuses = std::pair<std::string, std::string>;
  const auto status = [&](bool verify) {
    char* json = nullptr;
    EXPECT_EQ(sillok_model_check(PathToUtf8(models_dir_).c_str(), verify, &json), SILLOK_OK);
    const Json entries = Json::parse(Take(json));
    EXPECT_EQ(entries.size(), 3u);  // every file of every model
    EXPECT_EQ(entries[0]["model"], "fake-transcriber");
    EXPECT_EQ(entries[2]["model"], "fake-small");
    return Statuses(entries[0]["status"].get<std::string>(),
                    entries[1]["status"].get<std::string>());
  };
  EXPECT_EQ(status(true), Statuses("missing", "missing"));
  std::ofstream(models_dir_ / "fake-transcriber.bin") << "abd";
  std::ofstream(models_dir_ / "fake-encoder.zip") << "abc";
  EXPECT_EQ(status(false), Statuses("ok", "extract"));
  EXPECT_EQ(status(true).first, "wrong_hash");
  std::filesystem::create_directory(models_dir_ / "fake-encoder");
  EXPECT_EQ(status(true).second, "ok");
}

TEST_F(CapiTest, EngineTakesACatalogTranscriptionModel) {
  const auto create = [&](const char* model) {
    const Json config = {{"models_dir", PathToUtf8(models_dir_)}, {"transcription_model", model}};
    sillok_engine* engine = nullptr;
    const sillok_status status = sillok_engine_create(config.dump().c_str(), &engine);
    sillok_engine_destroy(engine);
    return status;
  };
  EXPECT_EQ(create("fake-small"), SILLOK_OK);
  EXPECT_EQ(create("no-such-model"), SILLOK_INVALID_ARGUMENT);
  EXPECT_NE(std::string(sillok_last_error()).find("no-such-model"), std::string::npos);
}

TEST_F(CapiTest, EngineModelsFollowTheTranscriptionModel) {
  const auto ids = [](const char* model) {
    char* out = nullptr;
    EXPECT_EQ(sillok_engine_models(model, &out), SILLOK_OK);
    std::vector<std::string> result;
    for (const Json& entry : Json::parse(Take(out))) result.push_back(entry["id"]);
    return result;
  };
  EXPECT_EQ(ids(nullptr), std::vector<std::string>{"fake-transcriber"});
  EXPECT_EQ(ids(""), std::vector<std::string>{"fake-transcriber"});
  EXPECT_EQ(ids("fake-small"), std::vector<std::string>{"fake-small"});
  char* out = nullptr;
  EXPECT_EQ(sillok_engine_models("no-such-model", &out), SILLOK_INVALID_ARGUMENT);
  EXPECT_EQ(out, nullptr);
}

TEST_F(CapiTest, ReadsAudioAndVideoFiles) {
  for (const char* name : {"tone.m4a", "tone.mp3", "tone.opus", "tone.flac", "tone.webm",
                           "lecture.mp4"}) {
    SCOPED_TRACE(name);
    sillok_job* job = Job(Engine());
    double last = -1;
    const auto progress = [](void* user, double fraction) {
      *static_cast<double*>(user) = fraction;
    };
    ASSERT_EQ(sillok_job_push_file(job, PathToUtf8(MediaFile(name)).c_str(), progress, &last),
              SILLOK_OK)
        << sillok_last_error();
    EXPECT_EQ(last, 1.0);
    ASSERT_EQ(sillok_job_end_audio(job), SILLOK_OK);
    // One second, without the encoders' priming and padding. The samples
    // themselves: AudioFileTest.
    EXPECT_NEAR(static_cast<double>(DurationMs(job)), 1000, 7);
  }
}

TEST_F(CapiTest, ReadsWavFilesWithUnicodeNames) {
  const auto path = models_dir_ / PathFromUtf8("녹음 1.wav");
  WriteWav(path, 22050, 2, 0.5);
  sillok_job* job = Job(Engine());
  ASSERT_EQ(sillok_job_push_file(job, PathToUtf8(path).c_str(), nullptr, nullptr), SILLOK_OK)
      << sillok_last_error();
  ASSERT_EQ(sillok_job_end_audio(job), SILLOK_OK);
  EXPECT_EQ(DurationMs(job), 500);
}

TEST_F(CapiTest, RefusesFilesWithoutAudio) {
  sillok_job* job = Job(Engine());
  const auto missing = PathToUtf8(models_dir_ / "missing.m4a");
  EXPECT_EQ(sillok_job_push_file(job, missing.c_str(), nullptr, nullptr), SILLOK_MEDIA_ERROR);
  EXPECT_NE(std::string(sillok_last_error()).find("missing.m4a"), std::string::npos);
  const auto text = models_dir_ / "notes.txt";
  std::ofstream(text) << "not audio\n";
  EXPECT_EQ(sillok_job_push_file(job, PathToUtf8(text).c_str(), nullptr, nullptr),
            SILLOK_MEDIA_ERROR);
  EXPECT_EQ(sillok_job_push_file(job, nullptr, nullptr, nullptr), SILLOK_INVALID_ARGUMENT);
}

TEST_F(CapiTest, SavesTheRecordingForListeningBack) {
  sillok_job* job = Job(Engine());
  ASSERT_EQ(sillok_job_push_file(job, PathToUtf8(MediaFile("tone.flac")).c_str(), nullptr,
                                 nullptr),
            SILLOK_OK);
  const auto copy = models_dir_ / PathFromUtf8("사본.m4a");
  EXPECT_EQ(sillok_job_save_audio(job, PathToUtf8(copy).c_str(), nullptr, nullptr),
            SILLOK_INVALID_ARGUMENT);  // the recording has not ended
  ASSERT_EQ(sillok_job_end_audio(job), SILLOK_OK);
  double last = -1;
  const auto progress = [](void* user, double fraction) { *static_cast<double*>(user) = fraction; };
  ASSERT_EQ(sillok_job_save_audio(job, PathToUtf8(copy).c_str(), progress, &last), SILLOK_OK)
      << sillok_last_error();
  EXPECT_EQ(last, 1.0);
  EXPECT_LT(std::filesystem::file_size(copy), 12000u);  // 64 kbit/s

  // Read back, the copy is as long as the recording. That it lines up with
  // it: AudioFileTest.
  sillok_job* again = Job(Engine());
  ASSERT_EQ(sillok_job_push_file(again, PathToUtf8(copy).c_str(), nullptr, nullptr), SILLOK_OK)
      << sillok_last_error();
  ASSERT_EQ(sillok_job_end_audio(again), SILLOK_OK);
  EXPECT_EQ(DurationMs(again), DurationMs(job));

  const auto missing_dir = models_dir_ / "no such dir" / "copy.m4a";
  EXPECT_EQ(sillok_job_save_audio(job, PathToUtf8(missing_dir).c_str(), nullptr, nullptr),
            SILLOK_MEDIA_ERROR);
}

TEST_F(CapiTest, CancelStopsReadingAFile) {
  sillok_job* job = Job(Engine());
  sillok_job_cancel(job);
  EXPECT_EQ(sillok_job_push_file(job, PathToUtf8(MediaFile("tone.m4a")).c_str(), nullptr, nullptr),
            SILLOK_CANCELLED);
}

TEST_F(CapiTest, VersionAndDevices) {
  EXPECT_EQ(std::string(sillok_version()).rfind("sillok ", 0), 0u);
  char* out = nullptr;
  ASSERT_EQ(sillok_devices(&out), SILLOK_OK);
  EXPECT_EQ(Json::parse(Take(out))[0]["type"], "cpu");
}

}  // namespace
}  // namespace sillok
