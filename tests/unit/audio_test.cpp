#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <vector>

#include "core/audio_buffer.hpp"
#include "core/level.hpp"
#include "core/resampler.hpp"

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

double Rms(std::span<const float> pcm) {
  double sum = 0.0;
  for (const float x : pcm) sum += static_cast<double>(x) * static_cast<double>(x);
  return std::sqrt(sum / static_cast<double>(pcm.size()));
}

std::vector<float> Resample(int rate, std::span<const float> input,
                            std::size_t block) {
  Resampler resampler(rate);
  std::vector<float> out;
  for (std::size_t i = 0; i < input.size(); i += block) {
    resampler.Process(input.subspan(i, std::min(block, input.size() - i)), out);
  }
  resampler.Finish(out);
  return out;
}

TEST(ResamplerTest, PassesThroughAt16k) {
  const auto input = Sine(16000, 440.0, 0.5);
  EXPECT_EQ(Resample(16000, input, 1000), input);
}

TEST(ResamplerTest, OutputLengthMatchesDuration) {
  for (const int rate : {8000, 22050, 44100, 48000, 96000}) {
    const auto input = Sine(rate, 300.0, 1.37);
    const auto out = Resample(rate, input, 4096);
    const auto expected = static_cast<double>(input.size()) * 16000.0 / rate;
    EXPECT_NEAR(static_cast<double>(out.size()), expected, 1.0) << rate;
  }
}

TEST(ResamplerTest, KeepsPassbandToneAndPhase) {
  for (const int rate : {8000, 44100, 48000}) {
    const auto out = Resample(rate, Sine(rate, 1000.0, 1.0), 777);
    const auto expected = Sine(16000, 1000.0, 1.0);
    // Ignore the filter's edges; the body must match the ideal 16 kHz tone.
    double error = 0.0;
    for (std::size_t i = 1000; i + 1000 < expected.size(); ++i) {
      error = std::max(error, std::abs(static_cast<double>(out[i]) -
                                       static_cast<double>(expected[i])));
    }
    EXPECT_LT(error, 0.01) << rate;
  }
}

TEST(ResamplerTest, RemovesContentAboveNewNyquist) {
  // A 12 kHz tone cannot exist at 16 kHz and must not alias into the band.
  const auto out = Resample(48000, Sine(48000, 12000.0, 1.0), 4096);
  const std::span<const float> body(out.data() + 1000, out.size() - 2000);
  EXPECT_LT(Rms(body), 0.5 / std::sqrt(2.0) * 0.01);  // below -40 dB
}

TEST(ResamplerTest, BlockSizeDoesNotChangeOutput) {
  const auto input = Sine(44100, 523.0, 0.7);
  const auto whole = Resample(44100, input, input.size());
  const auto pieces = Resample(44100, input, 333);
  ASSERT_EQ(whole.size(), pieces.size());
  for (std::size_t i = 0; i < whole.size(); ++i) {
    ASSERT_FLOAT_EQ(whole[i], pieces[i]) << i;
  }
}

TEST(ResamplerTest, RejectsUnsupportedRates) {
  EXPECT_THROW(Resampler(4000), std::invalid_argument);
  EXPECT_THROW(Resampler(400000), std::invalid_argument);
}

TEST(AudioBufferTest, StoresSixteenBitMono) {
  AudioBuffer audio;
  audio.Push(std::vector<float>{0.5f, -0.25f, 1.5f, -2.0f}, 16000, 1);
  audio.Finish();
  ASSERT_EQ(audio.size(), 4);
  const auto pcm = audio.Read({0, 4});
  EXPECT_FLOAT_EQ(pcm[0], 0.5f);
  EXPECT_FLOAT_EQ(pcm[1], -0.25f);
  EXPECT_FLOAT_EQ(pcm[2], 32767.0f / 32768.0f);  // clipped
  EXPECT_FLOAT_EQ(pcm[3], -1.0f);
}

TEST(AudioBufferTest, AveragesChannels) {
  AudioBuffer audio;
  audio.Push(std::vector<float>{0.5f, 0.0f, -0.5f, -0.25f}, 16000, 2);
  audio.Finish();
  ASSERT_EQ(audio.size(), 2);
  const auto pcm = audio.Read({0, 2});
  EXPECT_FLOAT_EQ(pcm[0], 0.25f);
  EXPECT_FLOAT_EQ(pcm[1], -0.375f);
}

TEST(AudioBufferTest, ResamplesAcrossPushes) {
  AudioBuffer audio;
  const auto input = Sine(48000, 440.0, 2.0);
  for (std::size_t i = 0; i < input.size(); i += 1000) {
    audio.Push(std::span(input).subspan(i, std::min<std::size_t>(1000, input.size() - i)),
               48000, 1);
  }
  audio.Finish();
  EXPECT_EQ(audio.size(), 32000);
}

TEST(AudioBufferTest, RejectsFormatChangesAndLatePushes) {
  AudioBuffer audio;
  audio.Push(std::vector<float>(10, 0.0f), 16000, 1);
  EXPECT_THROW(audio.Push(std::vector<float>(10, 0.0f), 44100, 1),
               std::invalid_argument);
  EXPECT_THROW(audio.Push(std::vector<float>(10, 0.0f), 16000, 2),
               std::invalid_argument);
  EXPECT_THROW(audio.Push(std::vector<float>(3, 0.0f), 16000, 2),
               std::invalid_argument);
  audio.Finish();
  EXPECT_THROW(audio.Push(std::vector<float>(10, 0.0f), 16000, 1),
               std::logic_error);
}

TEST(AudioBufferTest, ReadClampsAndSpansBlocks) {
  AudioBuffer audio;
  std::vector<float> pcm((1 << 20) + 100);
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    pcm[i] = static_cast<float>(i % 100) / 1000.0f;
  }
  audio.Push(pcm, 16000, 1);
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
  audio.Push(pcm, 16000, 1);
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
    audio.Push(pcm, 16000, 1);
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
  auto pcm = Sine(16000, 300.0, 4.0, 0.3);
  pcm.resize(pcm.size() + 16000 * 2, 0.0f);
  const auto quiet = Sine(16000, 300.0, 4.0, 0.003);
  pcm.insert(pcm.end(), quiet.begin(), quiet.end());
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

}  // namespace
}  // namespace sillok
