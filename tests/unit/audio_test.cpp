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

AudioBuffer BufferOf(std::span<const float> pcm) {
  AudioBuffer audio;
  audio.Push(pcm);
  audio.Finish();
  return audio;
}

TEST(LevelTest, MeasuresTheSpeechWithoutThePauses) {
  // Speech at -46 dBFS around a 5 s pause of noise at -69 dBFS.
  const auto speech = Sine(16000, 300.0, 3.0, 0.007);
  const auto noise = Sine(16000, 50.0, 5.0, 0.0005);
  std::vector<float> pcm = speech;
  pcm.insert(pcm.end(), noise.begin(), noise.end());
  pcm.insert(pcm.end(), speech.begin(), speech.end());
  const double to_reference = 0.05 / (0.007 / std::sqrt(2.0));  // -26 dBFS
  EXPECT_NEAR(LoudnessGain(BufferOf(pcm)), to_reference, to_reference * 0.02);
  EXPECT_NEAR(LoudnessGain(BufferOf(speech)), to_reference, to_reference * 0.02);
}

TEST(LevelTest, NeverLowersAndRaisesBy30dBAtMost) {
  EXPECT_EQ(LoudnessGain(BufferOf(Sine(16000, 300.0, 3.0, 0.3))), 1.0);
  EXPECT_NEAR(LoudnessGain(BufferOf(Sine(16000, 300.0, 3.0, 0.0005))), 31.6, 1e-9);
  EXPECT_EQ(LoudnessGain(BufferOf(std::vector<float>(16000 * 3, 0.0f))), 1.0);
  EXPECT_EQ(LoudnessGain(BufferOf(std::vector<float>{})), 1.0);
}

TEST(LevelTest, AppliesTheGainAndBendsPeaksBelowOne) {
  std::vector<float> pcm = {0.01f, -0.05f, 0.2f, -0.3f, 0.6f};
  ApplyGain(4.0, pcm);
  EXPECT_FLOAT_EQ(pcm[0], 0.04f);
  EXPECT_FLOAT_EQ(pcm[1], -0.2f);
  EXPECT_FLOAT_EQ(pcm[2], 0.8f);
  EXPECT_LT(pcm[3], -0.9f);  // -1.2, bent
  EXPECT_GT(pcm[3], -1.0f);
  EXPECT_GT(pcm[4], pcm[3] * -1.0f);  // still in order
  EXPECT_LE(pcm[4], 1.0f);            // 2.4 comes as close to 1 as a float gets
  std::vector<float> loud = {0.95f, -0.99f};
  ApplyGain(1.0, loud);
  EXPECT_EQ(loud, (std::vector<float>{0.95f, -0.99f}));
}

}  // namespace
}  // namespace sillok
