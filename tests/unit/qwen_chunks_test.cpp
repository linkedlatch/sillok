#include <gtest/gtest.h>

#include <vector>

#include "backends/qwen/qwen_chunks.hpp"
#include "core/audio_buffer.hpp"

namespace sillok::qwen {
namespace {

// Loud noise-like samples, with silence over `quiet` (in seconds).
AudioBuffer Recording(double seconds, std::vector<std::pair<double, double>> quiet = {}) {
  std::vector<float> pcm(static_cast<std::size_t>(seconds * kSampleRate));
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    pcm[i] = (i * 2654435761u >> 16) % 2 == 0 ? 0.5f : -0.5f;
  }
  for (const auto& [from, to] : quiet) {
    for (auto i = static_cast<std::size_t>(from * kSampleRate);
         i < static_cast<std::size_t>(to * kSampleRate) && i < pcm.size(); ++i) {
      pcm[i] = 0.0f;
    }
  }
  AudioBuffer audio;
  audio.Push(pcm);
  audio.Finish();
  return audio;
}

void ExpectCovers(const std::vector<SampleRange>& chunks, SampleIndex total) {
  ASSERT_FALSE(chunks.empty());
  EXPECT_EQ(chunks.front().begin, 0);
  EXPECT_EQ(chunks.back().end, total);
  for (std::size_t i = 1; i < chunks.size(); ++i) EXPECT_EQ(chunks[i].begin, chunks[i - 1].end);
}

TEST(QwenChunksTest, KeepsUpTo180SecondsWhole) {
  const AudioBuffer audio = Recording(180.0);
  const auto chunks = AudioChunks(audio);
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_EQ(chunks[0].end, audio.size());
}

TEST(QwenChunksTest, CutsAtTheQuietestPlaceNearEach180SecondMark) {
  // Quiet stretches 3 s after the first mark and 4 s before the second
  // (counted from the first cut); one 7 s away is out of reach.
  const AudioBuffer audio = Recording(400.0, {{183.0, 183.5}, {359.0, 359.4}, {190.0, 192.0}});
  const auto chunks = AudioChunks(audio);
  ExpectCovers(chunks, audio.size());
  ASSERT_EQ(chunks.size(), 3u);
  EXPECT_GE(chunks[0].end, ToSamples(Milliseconds{183000}));
  EXPECT_LT(chunks[0].end, ToSamples(Milliseconds{183500}));
  EXPECT_GE(chunks[1].end, ToSamples(Milliseconds{359000}));
  EXPECT_LT(chunks[1].end, ToSamples(Milliseconds{359400}));
  for (const SampleRange& chunk : chunks) EXPECT_LE(chunk.length(), kMaxChunk + kCutSearch);
}

TEST(QwenChunksTest, CutsLoudAudioWithinFiveSecondsOfTheMark) {
  const AudioBuffer audio = Recording(1000.0);
  const auto chunks = AudioChunks(audio);
  ExpectCovers(chunks, audio.size());
  ASSERT_GE(chunks.size(), 5u);
  for (std::size_t i = 0; i + 1 < chunks.size(); ++i) {
    EXPECT_GE(chunks[i].length(), kMaxChunk - kCutSearch);
    EXPECT_LE(chunks[i].length(), kMaxChunk + kCutSearch);
  }
}

}  // namespace
}  // namespace sillok::qwen
