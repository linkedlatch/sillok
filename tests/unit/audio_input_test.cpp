#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

#include "core/audio_buffer.hpp"
#include "media/audio_input.hpp"

namespace sillok {
namespace {

std::vector<float> Sine(int rate, double frequency, double seconds) {
  std::vector<float> pcm(static_cast<std::size_t>(rate * seconds));
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    pcm[i] = static_cast<float>(
        0.5 * std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(i) / rate));
  }
  return pcm;
}

double Rms(std::span<const float> pcm) {
  double sum = 0.0;
  for (const float x : pcm) sum += static_cast<double>(x) * static_cast<double>(x);
  return std::sqrt(sum / static_cast<double>(pcm.size()));
}

// `input` pushed in blocks of `block` frames, as stored at 16 kHz mono.
std::vector<float> Convert(int rate, std::span<const float> input, std::size_t block,
                           int channels = 1) {
  AudioBuffer audio;
  AudioInput converter(audio);
  const std::size_t step = block * static_cast<std::size_t>(channels);
  for (std::size_t i = 0; i < input.size(); i += step) {
    converter.Push(input.subspan(i, std::min(step, input.size() - i)), rate, channels);
  }
  converter.Finish();
  return audio.Read({0, audio.size()});
}

TEST(AudioInputTest, PassesThroughAt16k) {
  const auto input = Sine(16000, 440.0, 0.5);
  const auto out = Convert(16000, input, 1000);
  ASSERT_EQ(out.size(), input.size());
  for (std::size_t i = 0; i < out.size(); ++i) {
    ASSERT_NEAR(out[i], input[i], 1.0 / 32768.0) << i;
  }
}

TEST(AudioInputTest, AveragesChannels) {
  const auto out = Convert(16000, std::vector<float>{0.5f, 0.0f, -0.5f, -0.25f}, 2, 2);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_FLOAT_EQ(out[0], 0.25f);
  EXPECT_FLOAT_EQ(out[1], -0.375f);
}

TEST(AudioInputTest, OutputLengthMatchesDuration) {
  for (const int rate : {8000, 22050, 44100, 48000, 96000}) {
    const auto input = Sine(rate, 300.0, 1.37);
    const auto out = Convert(rate, input, 4096);
    const auto expected = static_cast<double>(input.size()) * 16000.0 / rate;
    EXPECT_NEAR(static_cast<double>(out.size()), expected, 1.0) << rate;
  }
}

TEST(AudioInputTest, KeepsTiming) {
  for (const int rate : {8000, 44100, 48000}) {
    std::vector<float> click(static_cast<std::size_t>(rate * 2), 0.0f);
    click[static_cast<std::size_t>(rate)] = 1.0f;  // at 1 s
    const auto out = Convert(rate, click, 1000);
    const auto peak = std::max_element(out.begin(), out.end(), [](float a, float b) {
      return std::abs(a) < std::abs(b);
    });
    EXPECT_EQ(peak - out.begin(), 16000) << rate;
  }
}

TEST(AudioInputTest, KeepsPassbandToneAndPhase) {
  for (const int rate : {8000, 44100, 48000}) {
    const auto out = Convert(rate, Sine(rate, 1000.0, 1.0), 777);
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

TEST(AudioInputTest, RemovesContentAboveNewNyquist) {
  // A 12 kHz tone cannot exist at 16 kHz and must not alias into the band.
  const auto out = Convert(48000, Sine(48000, 12000.0, 1.0), 4096);
  const std::span<const float> body(out.data() + 1000, out.size() - 2000);
  EXPECT_LT(Rms(body), 0.5 / std::sqrt(2.0) * 0.01);  // below -40 dB
}

TEST(AudioInputTest, BlockSizeDoesNotChangeOutput) {
  const auto input = Sine(44100, 523.0, 0.7);
  EXPECT_EQ(Convert(44100, input, input.size()), Convert(44100, input, 333));
}

TEST(AudioInputTest, RejectsUnsupportedRates) {
  AudioBuffer audio;
  AudioInput converter(audio);
  EXPECT_THROW(converter.Push(std::vector<float>(10, 0.0f), 4000, 1), std::invalid_argument);
  EXPECT_THROW(converter.Push(std::vector<float>(10, 0.0f), 400000, 1),
               std::invalid_argument);
}

TEST(AudioInputTest, RejectsFormatChangesAndLatePushes) {
  AudioBuffer audio;
  AudioInput converter(audio);
  converter.Push(std::vector<float>(10, 0.0f), 16000, 1);
  EXPECT_THROW(converter.Push(std::vector<float>(10, 0.0f), 44100, 1),
               std::invalid_argument);
  EXPECT_THROW(converter.Push(std::vector<float>(10, 0.0f), 16000, 2),
               std::invalid_argument);
  EXPECT_THROW(converter.Push(std::vector<float>(3, 0.0f), 16000, 2),
               std::invalid_argument);
  converter.Finish();
  EXPECT_TRUE(audio.finished());
  EXPECT_THROW(converter.Push(std::vector<float>(10, 0.0f), 16000, 1), std::logic_error);
}

}  // namespace
}  // namespace sillok
