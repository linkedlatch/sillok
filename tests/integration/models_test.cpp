// Integration tests with the real models. They need the model files and the
// test recordings (scripts/fetch-test-audio.sh) and skip when those are
// missing.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "backends/models.hpp"
#include "backends/qwen/qwen_transcriber.hpp"
#include "core/cancel.hpp"
#include "media/audio_file.hpp"
#include "media/audio_input.hpp"

namespace sillok {
namespace {

const std::filesystem::path kModels = SILLOK_TEST_MODELS_DIR;
const std::filesystem::path kAudio = SILLOK_TEST_AUDIO_DIR;

// The first `seconds` of a recording at 16 kHz mono; empty if it is missing.
std::vector<float> ReadWav(const std::filesystem::path& path, double seconds = 1e9) {
  if (!std::filesystem::exists(path)) return {};
  AudioBuffer audio;
  AudioInput input(audio);
  DecodeAudioFile(path, input, {}, StopToken());
  input.Finish();
  return audio.Read({0, std::min(audio.size(), static_cast<SampleIndex>(seconds * 16000))});
}

// `pcm` as the pipeline hears a recording of it alone.
std::vector<float> EvenedOut(std::span<const float> pcm) {
  AudioBuffer audio;
  AudioInput input(audio);
  input.Push(pcm, kSampleRate, 1);
  input.Finish();
  return audio.Read({0, audio.size()});
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
    return OpenModels({.models_dir = kModels, .device = device, .gpu_index = 0, .threads = 0,
                       .transcription_model = ""});
  }
};

TEST_F(ModelsTest, VoiceDetectionFindsQuietSpeechAndIsDeterministic) {
  // A recording near -50 dBFS (the set's -30 dBFS, 20 dB lower), raised as
  // the pipeline raises it.
  std::vector<float> pcm = ReadWav(kAudio / "sets" / "fleurs_en.wav", 30.0);
  for (float& x : pcm) x *= 0.1f;
  pcm = EvenedOut(pcm);
  pcm.resize(pcm.size() / 512 * 512);
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
  auto transcriber = Models()->OpenTranscriber();
  std::string text;
  transcriber->Transcribe(
      {.pcm = pcm, .language = "en", .glossary = {}},
      [&](const TranscribedWords& piece) {
        for (const Word& word : piece.words) text += " " + word.text;
      },
      [](double) {}, {});
  for (const char* word : {"subsequently", "relocated", "Hospital", "Cambridge"}) {
    EXPECT_NE(text.find(word), std::string::npos) << text;
  }
}

TEST_F(ModelsTest, DetectsLanguage) {
  std::vector<float> pcm = ReadWav(kAudio / "sets" / "fleurs_ko.wav", 15.0);
  auto models = Models();
  // The languages Qwen3-ForcedAligner knows (docs/DESIGN.md D43); "auto"
  // picks among them alone.
  EXPECT_EQ(models->Languages(), (std::vector<std::string>{"ko", "en", "ja", "zh", "yue", "de",
                                                            "es", "fr", "it", "pt", "ru"}));
  std::vector<std::string> detected;
  models->OpenTranscriber()->Transcribe(
      {.pcm = pcm, .language = "", .glossary = {}},
      [&](const TranscribedWords& piece) { detected.push_back(piece.language); }, [](double) {}, {});
  ASSERT_FALSE(detected.empty());
  for (const std::string& language : detected) EXPECT_EQ(language, "ko");
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

// Qwen3-ASR with Qwen3-ForcedAligner, opened directly (the catalog does not
// list it yet).
class QwenTest : public ::testing::Test {
 protected:
  void SetUp() override {
    for (const char* file : {"Qwen3-ASR-1.7B-Q8_0.gguf", "mmproj-Qwen3-ASR-1.7B-bf16.gguf",
                             "qwen3-forced-aligner-0.6b-q8_0.gguf",
                             "mmproj-qwen3-forced-aligner-0.6b-f16.gguf"}) {
      if (!std::filesystem::exists(kModels / file)) GTEST_SKIP() << file << " not found";
    }
    if (!std::filesystem::exists(kAudio / "sets" / "fleurs_en.wav")) {
      GTEST_SKIP() << "test recordings not found";
    }
  }
  static std::unique_ptr<Transcriber> Open() {
    return OpenQwen({.asr_model = kModels / "Qwen3-ASR-1.7B-Q8_0.gguf",
                     .asr_mmproj = kModels / "mmproj-Qwen3-ASR-1.7B-bf16.gguf",
                     .aligner_model = kModels / "qwen3-forced-aligner-0.6b-q8_0.gguf",
                     .aligner_mmproj = kModels / "mmproj-qwen3-forced-aligner-0.6b-f16.gguf",
                     .name = "qwen3-asr-1.7b",
                     .device = ChooseDevice(Device::kGpu, 0),
                     .threads = 8,
                     .languages = {"ko", "en"}});
  }
};

TEST_F(QwenTest, TranscribesAndAlignsEnglish) {
  // "He was subsequently relocated to Addenbrooke's Hospital in Cambridge."
  std::vector<float> pcm = ReadWav(kAudio / "sets" / "fleurs_en.wav", 372.0);
  pcm.erase(pcm.begin(), pcm.begin() + 364 * 16000);
  std::vector<TranscribedWords> pieces;
  double progress = 0.0;
  Open()->Transcribe({.pcm = pcm, .language = "", .glossary = {}},
                     [&](TranscribedWords piece) { pieces.push_back(std::move(piece)); },
                     [&](double fraction) {
                       EXPECT_GE(fraction, progress);
                       progress = fraction;
                     },
                     {});
  EXPECT_EQ(progress, 1.0);
  std::string text;
  Milliseconds last{0};
  for (const TranscribedWords& piece : pieces) {
    EXPECT_EQ(piece.language, "en");
    for (const Word& word : piece.words) {
      text += " " + word.text;
      EXPECT_GE(word.start, last) << word.text;
      EXPECT_GE(word.end, word.start) << word.text;
      EXPECT_LE(word.end, Milliseconds{8000}) << word.text;
      last = word.start;
    }
  }
  for (const char* word : {"subsequently", "relocated", "Hospital", "Cambridge."}) {
    EXPECT_NE(text.find(word), std::string::npos) << text;
  }
}

TEST_F(QwenTest, TellsKoreanAndStopsWhenAsked) {
  std::vector<float> pcm = ReadWav(kAudio / "sets" / "fleurs_ko.wav", 15.0);
  auto qwen = Open();
  std::vector<std::string> languages;
  qwen->Transcribe({.pcm = pcm, .language = "", .glossary = {}},
                   [&](TranscribedWords piece) { languages.push_back(piece.language); },
                   [](double) {}, {});
  ASSERT_FALSE(languages.empty());
  for (const std::string& language : languages) EXPECT_EQ(language, "ko");

  StopSource source;
  source.RequestStop();
  EXPECT_THROW(qwen->Transcribe({.pcm = pcm, .language = "ko", .glossary = {}},
                                [](TranscribedWords) {}, [](double) {}, source.Token()),
               Cancelled);
}

}  // namespace
}  // namespace sillok
