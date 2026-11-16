#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

#include "core/audio_buffer.hpp"

namespace sillok {
namespace {

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
  EXPECT_THROW(audio.Push(std::vector<float>(10, 0.0f)), std::invalid_argument);
}

TEST(AudioBufferTest, NonFiniteSamplesAreSafe) {
  AudioBuffer audio;
  const float inf = std::numeric_limits<float>::infinity();
  audio.Push(std::vector<float>{std::nanf(""), inf, -inf, -std::nanf("")});
  audio.Finish();
  const auto pcm = audio.Read({0, 4});
  EXPECT_FLOAT_EQ(pcm[0], 0.0f);
  EXPECT_FLOAT_EQ(pcm[1], 32767.0f / 32768.0f);
  EXPECT_FLOAT_EQ(pcm[2], -1.0f);
  EXPECT_FLOAT_EQ(pcm[3], 0.0f);
}

TEST(AudioBufferTest, ReadClampsToTheRecording) {
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

TEST(AudioBufferTest, JoinsPushesAndMoves) {
  AudioBuffer audio;
  std::vector<float> pcm(16000 * 70);  // past the first minute reserved
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    pcm[i] = static_cast<float>(i % 1000) / 2000.0f;
  }
  for (std::size_t i = 0; i < pcm.size(); i += 4096) {
    audio.Push(std::span(pcm).subspan(i, std::min<std::size_t>(4096, pcm.size() - i)));
  }
  audio.Finish();
  ASSERT_EQ(audio.size(), static_cast<SampleIndex>(pcm.size()));
  EXPECT_NEAR(audio.Read({123456, 123460})[0], pcm[123456], 1.0 / 32768.0);
  AudioBuffer moved = std::move(audio);
  EXPECT_TRUE(moved.finished());
  EXPECT_NEAR(moved.Read({1119999, 1120000})[0], pcm[1119999], 1.0 / 32768.0);
}

TEST(AudioBufferTest, ReadsFromSeveralThreadsAtOnce) {
  AudioBuffer audio;
  std::vector<float> pcm(16000 * 10);
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    pcm[i] = static_cast<float>(i % 777) / 1000.0f;
  }
  audio.Push(pcm);
  audio.Finish();
  const std::vector<float> expected = audio.Read({0, audio.size()});
  std::vector<int> mismatches(4, 0);
  std::vector<std::thread> threads;
  for (std::size_t t = 0; t < mismatches.size(); ++t) {
    threads.emplace_back([&, t] {
      for (SampleIndex begin = static_cast<SampleIndex>(t) * 1000; begin < audio.size();
           begin += 7919) {
        const std::vector<float> part = audio.Read({begin, begin + 3000});
        mismatches[t] += !std::equal(part.begin(), part.end(),
                                     expected.begin() + static_cast<std::ptrdiff_t>(begin));
      }
    });
  }
  for (std::thread& thread : threads) thread.join();
  EXPECT_EQ(mismatches, std::vector<int>(4, 0));
}
}  // namespace
}  // namespace sillok
