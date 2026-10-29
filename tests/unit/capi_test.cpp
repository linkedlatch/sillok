#include <gtest/gtest.h>

#include <cmath>
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
    const Millis start{500 + 400 * i};
    script.push_back({first_id + i, start, start + Millis{300}, "en", i < words / 2 ? 0 : 1});
  }
  const AudioBuffer audio = RenderScript(script, Millis{1000 + 400 * words});
  return audio.Read({0, audio.size()});
}

class CapiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    models_dir_ = std::filesystem::temp_directory_path() / PathFromUtf8("sillok 모델 capi");
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

TEST_F(CapiTest, ReadsTheRecordingWhileItIsPushed) {
  sillok_job* job = Job(Engine());
  // 1 s of a 440 Hz tone at 44.1 kHz stereo, pushed in decoder-sized blocks.
  std::vector<float> stereo(44100 * 2);
  for (std::size_t i = 0; i < stereo.size(); ++i) {
    stereo[i] = 0.5f * static_cast<float>(std::sin(2 * 3.14159265 * 440 * static_cast<double>(i / 2) / 44100));
  }
  std::vector<float> copy;
  uint64_t length = 0;
  const auto read_new = [&] {
    ASSERT_EQ(sillok_job_audio_length(job, &length), SILLOK_OK);
    std::vector<float> block(static_cast<std::size_t>(length) - copy.size());
    std::size_t read = 0;
    ASSERT_EQ(sillok_job_read_audio(job, copy.size(), block.data(), block.size(), &read), SILLOK_OK);
    ASSERT_EQ(read, block.size());
    copy.insert(copy.end(), block.begin(), block.end());
  };
  for (std::size_t frame = 0; frame < 44100; frame += 1024) {
    const std::size_t frames = std::min<std::size_t>(1024, 44100 - frame);
    ASSERT_EQ(sillok_job_push_audio(job, stereo.data() + frame * 2, frames, 44100, 2), SILLOK_OK);
    read_new();
  }
  EXPECT_LT(copy.size(), 16000u);  // the end is held back until the recording ends
  ASSERT_EQ(sillok_job_end_audio(job), SILLOK_OK);
  read_new();
  EXPECT_EQ(copy.size(), 16000u);

  // What was read piece by piece is what the job keeps.
  std::vector<float> whole(16000);
  std::size_t read = 0;
  ASSERT_EQ(sillok_job_read_audio(job, 0, whole.data(), whole.size() + 100, &read), SILLOK_OK);
  EXPECT_EQ(read, 16000u);
  EXPECT_EQ(whole, copy);
  ASSERT_EQ(sillok_job_read_audio(job, 16000, whole.data(), 10, &read), SILLOK_OK);
  EXPECT_EQ(read, 0u);
  EXPECT_EQ(sillok_job_read_audio(job, 16001, whole.data(), 10, &read), SILLOK_INVALID_ARGUMENT);

  EXPECT_EQ(sillok_job_push_audio(job, stereo.data(), 10, 44100, 2), SILLOK_INVALID_ARGUMENT);
  EXPECT_NE(std::string(sillok_last_error()).find("end_audio"), std::string::npos);
  ASSERT_EQ(sillok_job_end_audio(job), SILLOK_OK);  // again: no effect
  char* out = nullptr;
  ASSERT_EQ(sillok_job_run(job, nullptr, nullptr, &out), SILLOK_OK);
  EXPECT_EQ(Json::parse(Take(out))["duration_ms"], 1000);
  ASSERT_EQ(sillok_job_audio_length(job, &length), SILLOK_OK);
  EXPECT_EQ(length, 16000u);
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
  EXPECT_EQ(engine, nullptr);
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
  ASSERT_EQ(sillok_export(edited.c_str(), "srt", nullptr, &out), SILLOK_OK);
  EXPECT_EQ(Take(out),
            "1\n00:00:00,000 --> 00:00:01,200\n김교수: 안녕하세요\n\n"
            "2\n00:00:01,500 --> 00:00:03,000\nSpeaker 2: 질문 있습니다\n\n");
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
  EXPECT_EQ(Json::parse(Take(out)).size(), 2u);

  using Statuses = std::pair<std::string, std::string>;
  const auto status = [&](bool verify) {
    char* json = nullptr;
    EXPECT_EQ(sillok_model_check(PathToUtf8(models_dir_).c_str(), verify, &json), SILLOK_OK);
    const Json entries = Json::parse(Take(json));
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

TEST_F(CapiTest, VersionAndDevices) {
  EXPECT_EQ(std::string(sillok_version()).rfind("sillok ", 0), 0u);
  char* out = nullptr;
  ASSERT_EQ(sillok_devices(&out), SILLOK_OK);
  EXPECT_EQ(Json::parse(Take(out))[0]["type"], "cpu");
}

}  // namespace
}  // namespace sillok
