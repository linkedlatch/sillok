// Integration tests with the real models. They need the model files and the
// test recordings (scripts/fetch-test-audio.sh) and skip when those are
// missing.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "backends/models.hpp"
#include "core/cancel.hpp"
#include "core/level.hpp"
#include "wav_reader.hpp"

namespace sillok {
namespace {

const std::filesystem::path kModels = SILLOK_TEST_MODELS_DIR;
const std::filesystem::path kAudio = SILLOK_TEST_AUDIO_DIR;

std::vector<float> ReadWav(const std::filesystem::path& path, double seconds = 1e9) {
  std::FILE* file = std::fopen(path.string().c_str(), "rb");
  if (file == nullptr) return {};
  cli::WavReader reader(file);
  std::vector<float> pcm;
  while (static_cast<double>(pcm.size()) < seconds * 16000) {
    const auto block = reader.Read(16000);
    if (block.empty()) break;
    pcm.insert(pcm.end(), block.begin(), block.end());
  }
  std::fclose(file);
  return pcm;
}

// Runs `work` and requests a stop after `delay`. Returns the time from the
// request to `work` ending with Cancelled, or nothing when `work` finished
// before the request.
template <typename Work>
std::optional<std::chrono::duration<double>> TimeToCancel(Work work,
                                                          std::chrono::milliseconds delay) {
  using Clock = std::chrono::steady_clock;
  std::stop_source source;
  std::atomic<Clock::rep> requested{0};
  std::jthread stopper([&](std::stop_token done) {
    const auto until = Clock::now() + delay;
    while (!done.stop_requested() && Clock::now() < until) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (done.stop_requested()) return;
    requested = Clock::now().time_since_epoch().count();
    source.request_stop();
  });
  try {
    work(source.get_token());
  } catch (const Cancelled&) {
    const Clock::time_point at{Clock::duration{requested.load()}};
    return Clock::now() - at;
  }
  stopper.request_stop();
  return std::nullopt;
}

class ModelsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::exists(kModels / "ggml-large-v3-turbo-q5_0.bin") ||
        !std::filesystem::exists(kAudio / "sets" / "fleurs_en.wav")) {
      GTEST_SKIP() << "models or test recordings not found";
    }
  }
  static std::unique_ptr<ModelProvider> Models(Device device = Device::kCpu) {
    return OpenModels({.models_dir = kModels, .device = device, .gpu_index = 0, .threads = 0});
  }
};

TEST_F(ModelsTest, VoiceDetectionFindsQuietSpeechAndIsDeterministic) {
  // The first 30 s of the English set include recordings near -50 dBFS.
  std::vector<float> pcm = ReadWav(kAudio / "sets" / "fleurs_en.wav", 30.0);
  pcm.resize(pcm.size() / 512 * 512);
  NormalizeLevel(pcm);
  auto models = Models();
  const auto first = models->OpenVoiceDetector()->Detect(pcm);
  const auto second = models->OpenVoiceDetector()->Detect(pcm);
  EXPECT_EQ(first, second);  // the LSTM state starts from zero every time
  const auto speech = std::count_if(first.begin(), first.end(), [](float p) { return p >= 0.5f; });
  EXPECT_GT(speech, static_cast<long>(first.size() / 3));
  EXPECT_NE(*std::min_element(first.begin(), first.end()),
            *std::max_element(first.begin(), first.end()));
}

TEST_F(ModelsTest, TranscribesEnglish) {
  // "He was subsequently relocated to Addenbrooke's Hospital in Cambridge."
  std::vector<float> pcm = ReadWav(kAudio / "sets" / "fleurs_en.wav", 372.0);
  pcm.erase(pcm.begin(), pcm.begin() + 364 * 16000);
  NormalizeLevel(pcm);
  auto transcriber = Models()->OpenTranscriber();
  std::string text;
  for (const TokenSegment& segment :
       transcriber->Transcribe({.pcm = pcm, .language = "en", .prompt = ""}, {})) {
    for (const Token& token : segment.tokens) text += token.text;
  }
  for (const char* word : {"subsequently", "relocated", "Hospital", "Cambridge"}) {
    EXPECT_NE(text.find(word), std::string::npos) << text;
  }
}

TEST_F(ModelsTest, DetectsLanguage) {
  std::vector<float> pcm = ReadWav(kAudio / "sets" / "fleurs_ko.wav", 15.0);
  NormalizeLevel(pcm);
  auto transcriber = Models()->OpenTranscriber();
  const std::vector<std::string> candidates = {"ko", "en"};
  const auto scores = transcriber->DetectLanguage(pcm, candidates, {});
  ASSERT_EQ(scores.size(), 2u);
  EXPECT_GT(scores[0].probability, scores[1].probability);
}

// Needs patches/whisper.cpp: whisper.cpp itself checks for an abort only
// between encoder passes, which take half a minute on a slow CPU.
TEST_F(ModelsTest, CancelsInTheMiddleOfAnEncoderPassOnCpu) {
  std::vector<float> pcm = ReadWav(kAudio / "sets" / "fleurs_en.wav", 28.0);
  NormalizeLevel(pcm);
  auto transcriber = Models(Device::kCpu)->OpenTranscriber();
  const std::vector<std::string> candidates = {"ko", "en"};
  const auto detect = TimeToCancel(
      [&](std::stop_token stop) { transcriber->DetectLanguage(pcm, candidates, stop); },
      std::chrono::milliseconds(300));
  const auto transcribe = TimeToCancel(
      [&](std::stop_token stop) {
        transcriber->Transcribe({.pcm = pcm, .language = "en", .prompt = ""}, stop);
      },
      std::chrono::milliseconds(300));
  if (!detect || !transcribe) GTEST_SKIP() << "finished before the cancel request";
  EXPECT_LT(detect->count(), 2.0);
  EXPECT_LT(transcribe->count(), 2.0);
}

TEST_F(ModelsTest, DiarizesMeeting) {
  const auto pcm = ReadWav(kAudio / "download" / "ES2004a.Mix-Headset.wav", 300.0);
  ASSERT_FALSE(pcm.empty());
  auto diarizer = Models()->OpenDiarizer();
  for (std::size_t i = 0; i < pcm.size(); i += 160000) {
    diarizer->Push(std::span(pcm).subspan(i, std::min<std::size_t>(160000, pcm.size() - i)));
  }
  std::set<int> speakers;
  for (const SpeakerTurn& turn : diarizer->Finish()) speakers.insert(turn.speaker);
  EXPECT_GE(speakers.size(), 2u);
}

TEST_F(ModelsTest, ReportsTheDeviceUsed) {
  EXPECT_EQ(Models(Device::kCpu)->OpenTranscriber()->Info().device, "CPU");
  bool has_gpu = false;
  for (const DeviceInfo& device : ListDevices()) has_gpu |= device.type == "gpu" || device.type == "igpu";
  const ModelInfo info = Models(Device::kGpu)->OpenTranscriber()->Info();
  EXPECT_EQ(info.fell_back_to_cpu, !has_gpu);
  if (!has_gpu) {
    EXPECT_EQ(info.device, "CPU");
  }
}

}  // namespace
}  // namespace sillok
