#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <vector>

#include "core/audio_buffer.hpp"
#include "core/level.hpp"

namespace sillok {
namespace {

std::vector<float> Sine(int rate, double frequency, double seconds,
                        double amplitude = 0.5) {
  std::vector<float> pcm(static_cast<std::size_t>(rate * seconds));
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    pcm[i] = static_cast<float>(
        amplitude *
        std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(i) / rate));
  }
  return pcm;
}

TEST(AudioBufferTest, StoresSixteenBitMono) {
  AudioBuffer audio;
  audio.Push(std::vector<float>{0.5f, -0.25f, 1.5f, -2.0f});
  audio.Finish();
  ASSERT_EQ(audio.size(), 4);
  const auto pcm = audio.Read({0, 4});
  EXPECT_FLOAT_EQ(pcm[0], 0.5f);
  EXPECT_FLOAT_EQ(pcm[1], -0.25f);
  EXPECT_FLOAT_EQ(pcm[2], 32767.0f / 32768.0f);  // clipped
  EXPECT_FLOAT_EQ(pcm[3], -1.0f);
}

TEST(AudioBufferTest, RejectsLatePushes) {
  AudioBuffer audio;
  audio.Push(std::vector<float>(10, 0.0f));
  audio.Finish();
  EXPECT_THROW(audio.Push(std::vector<float>(10, 0.0f)), std::logic_error);
}

TEST(AudioBufferTest, ReadClampsAndSpansBlocks) {
  AudioBuffer audio;
  std::vector<float> pcm((1 << 20) + 100);
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    pcm[i] = static_cast<float>(i % 100) / 1000.0f;
  }
  audio.Push(pcm);
  audio.Finish();
  const auto around = audio.Read({(1 << 20) - 5, (1 << 20) + 5});
  ASSERT_EQ(around.size(), 10u);
  EXPECT_NEAR(around[5], pcm[1 << 20], 1.0 / 32768.0);
  EXPECT_EQ(audio.Read({-10, 3}).size(), 3u);
  EXPECT_EQ(audio.Read({audio.size() - 2, audio.size() + 50}).size(), 2u);
}

TEST(AudioBufferTest, FrameEnergy) {
  AudioBuffer audio;
  std::vector<float> pcm(400, 0.0f);
  std::fill(pcm.begin() + 160, pcm.begin() + 320, 0.5f);
  audio.Push(pcm);
  audio.Finish();
  const auto energy = audio.FrameEnergy(160);
  ASSERT_EQ(energy.size(), 3u);
  EXPECT_FLOAT_EQ(energy[0], 0.0f);
  EXPECT_FLOAT_EQ(energy[1], 0.25f);
  EXPECT_FLOAT_EQ(energy[2], 0.0f);
}

std::filesystem::path FreshDirectory(const char* name) {
  const auto dir = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

TEST(AudioBufferTest, KeepsSamplesInATemporaryFile) {
  const auto dir = FreshDirectory("sillok-audio-buffer-test");
  {
    AudioBuffer audio(dir);
    std::vector<float> pcm(16000 * 10);
    for (std::size_t i = 0; i < pcm.size(); ++i) {
      pcm[i] = static_cast<float>(i % 1000) / 2000.0f;
    }
    audio.Push(pcm);
    audio.Finish();
    const auto middle = audio.Read({123456, 123460});
    EXPECT_NEAR(middle[0], pcm[123456], 1.0 / 32768.0);
    AudioBuffer moved = std::move(audio);
    EXPECT_EQ(moved.size(), 160000);
    EXPECT_NEAR(moved.Read({159999, 160000})[0], pcm[159999], 1.0 / 32768.0);
#ifndef _WIN32
    // The file has no name while in use, so nothing can be left behind.
    EXPECT_TRUE(std::filesystem::is_empty(dir));
#endif
  }
  EXPECT_TRUE(std::filesystem::is_empty(dir));
  std::filesystem::remove_all(dir);
}

TEST(AudioBufferTest, RemovesFilesLeftByEarlierRuns) {
  const auto dir = FreshDirectory("sillok-audio-leftover-test");
  std::ofstream(dir / "sillok-audio-00000000deadbeef.pcm") << "old";
  std::ofstream(dir / "unrelated.pcm") << "keep";
  { AudioBuffer audio(dir); }
  EXPECT_FALSE(std::filesystem::exists(dir / "sillok-audio-00000000deadbeef.pcm"));
  EXPECT_TRUE(std::filesystem::exists(dir / "unrelated.pcm"));
  std::filesystem::remove_all(dir);
}

TEST(AudioBufferTest, NeedsAWritableDirectory) {
  EXPECT_THROW(AudioBuffer(std::filesystem::temp_directory_path() / "sillok-no-such-dir"),
               std::runtime_error);
}

double PeakOf(std::span<const float> pcm) {
  double peak = 0.0;
  for (const float x : pcm) peak = std::max(peak, std::abs(static_cast<double>(x)));
  return peak;
}

TEST(LevelTest, RaisesQuietSpeechNextToLoudSpeech) {
  // A loud sentence, a pause, then one 40 dB quieter.
  const auto loud = Sine(16000, 300.0, 4.0, 0.3);
  const auto quiet = Sine(16000, 300.0, 4.0, 0.003);
  std::vector<float> pcm(loud.size() + 16000 * 2 + quiet.size(), 0.0f);
  std::copy(loud.begin(), loud.end(), pcm.begin());
  std::copy(quiet.begin(), quiet.end(), pcm.end() - static_cast<std::ptrdiff_t>(quiet.size()));
  const std::vector<float> original = pcm;
  NormalizeLevel(pcm);
  const std::span<const float> all(pcm);
  EXPECT_NEAR(PeakOf(all.subspan(0, 16000 * 2)), 0.3, 1e-6);  // never lowered
  const double raised = PeakOf(all.subspan(16000 * 7, 16000 * 2));
  EXPECT_GT(raised, 0.003 * 20);  // close to 30 dB more
  EXPECT_LE(raised, 0.003 * 31.7);
  EXPECT_LE(PeakOf(all), 0.95 + 1e-6);
}

TEST(LevelTest, KeepsSilenceQuiet) {
  std::vector<float> pcm(16000 * 3, 0.0001f);
  NormalizeLevel(pcm);
  EXPECT_LE(PeakOf(pcm), 0.0001 * 31.7);
}

double RmsOf(std::span<const float> pcm) {
  double sum = 0.0;
  for (const float x : pcm) sum += static_cast<double>(x) * static_cast<double>(x);
  return std::sqrt(sum / static_cast<double>(pcm.size()));
}

TEST(LevelTest, HoldsTheGainOfSpeechThroughAPause) {
  // Quiet speech, a 5 s pause whose noise is 10 dB below it, speech again.
  const auto speech = Sine(16000, 300.0, 3.0, 0.007);
  const auto noise = Sine(16000, 50.0, 5.0, 0.0022);
  std::vector<float> pcm = speech;
  pcm.insert(pcm.end(), noise.begin(), noise.end());
  pcm.insert(pcm.end(), speech.begin(), speech.end());
  const std::vector<float> original = pcm;
  NormalizeLevel(pcm);
  const auto gain = [&](double from, double seconds) {
    const auto first = static_cast<std::size_t>(from * 16000);
    const auto count = static_cast<std::size_t>(seconds * 16000);
    return RmsOf(std::span<const float>(pcm).subspan(first, count)) /
           RmsOf(std::span<const float>(original).subspan(first, count));
  };
  const double speech_gain = 0.05 / (0.007 / std::sqrt(2.0));  // to -26 dBFS
  EXPECT_NEAR(gain(0.0, 2.0), speech_gain, 0.3);
  EXPECT_NEAR(gain(4.5, 2.0), speech_gain, 0.3);  // not the 30 dB the noise alone would get
}

TEST(LevelTest, LeavesAudioWithoutSpeechAsItIs) {
  std::vector<float> pcm = Sine(16000, 50.0, 3.0, 0.0022);
  const std::vector<float> original = pcm;
  NormalizeLevel(pcm);
  EXPECT_EQ(pcm, original);
}

}  // namespace
}  // namespace sillok
