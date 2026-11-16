#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "core/segmenter.hpp"

namespace sillok {
namespace {

constexpr int kFrame = 512;  // 32 ms

// Probabilities for frames [0, frames) with speech in the given frame ranges.
std::vector<float> Probabilities(std::size_t frames,
                                 std::vector<std::pair<int, int>> speech) {
  std::vector<float> p(frames, 0.0f);
  for (const auto& [first, last] : speech) {
    for (int i = first; i < last; ++i) p[static_cast<std::size_t>(i)] = 0.9f;
  }
  return p;
}

SampleIndex Frames(int count) { return SampleIndex{count} * kFrame; }

TEST(FindSpeechTest, BridgesPausesDropsBlipsAndPads) {
  SegmenterOptions options;
  options.min_silence = Milliseconds{500};
  options.min_speech = Milliseconds{250};
  options.before = Milliseconds{100};
  options.after = Milliseconds{100};
  // Speech 10-40, pause of 5 frames (160 ms), speech 45-60, blip 100-102,
  // speech 200-260 after a long pause.
  const auto p = Probabilities(300, {{10, 40}, {45, 60}, {100, 102}, {200, 260}});
  const auto speech = FindSpeech(p, kFrame, Frames(300), options);
  ASSERT_EQ(speech.size(), 2u);
  EXPECT_EQ(speech[0].begin, Frames(10) - 1600);
  EXPECT_EQ(speech[0].end, Frames(60) + 1600);
  EXPECT_EQ(speech[1].begin, Frames(200) - 1600);
  EXPECT_EQ(speech[1].end, Frames(260) + 1600);
}

TEST(FindSpeechTest, UsesHysteresis) {
  SegmenterOptions options;
  options.before = Milliseconds{0};
  options.after = Milliseconds{0};
  std::vector<float> p = {0.1f, 0.6f, 0.4f, 0.4f, 0.3f, 0.1f};
  p.resize(20, 0.0f);
  std::fill(p.begin() + 1, p.begin() + 12, 0.4f);  // stays above 0.35
  p[1] = 0.6f;
  const auto speech = FindSpeech(p, kFrame, Frames(20), options);
  ASSERT_EQ(speech.size(), 1u);
  EXPECT_EQ(speech[0].begin, Frames(1));
  EXPECT_EQ(speech[0].end, Frames(12));
}

TEST(FindSpeechTest, KeepsTheTailAfterSpeech) {
  SegmenterOptions options;
  options.before = Milliseconds{10};
  options.after = Milliseconds{110};
  const auto speech = FindSpeech(Probabilities(300, {{100, 200}}), kFrame, Frames(300), options);
  ASSERT_EQ(speech.size(), 1u);
  EXPECT_EQ(speech[0].begin, Frames(100) - 160);
  EXPECT_EQ(speech[0].end, Frames(200) + 160 + 1600);
}

SampleIndex At(int ms) { return ToSamples(Milliseconds{ms}); }

TEST(ChunkSpeechTest, FillsChunksWhateverThePauses) {
  // 1 s regions: 0, 1.5 s, then 60 s later, 61.5 s and 63 s; at most 3.2 s
  // joined, so 1 s + 0.1 s + 1 s + 0.1 s + 1 s fits exactly and a fourth
  // does not.
  const std::vector<SampleRange> speech = {{At(0), At(1000)},
                                           {At(1500), At(2500)},
                                           {At(61500), At(62500)},
                                           {At(63000), At(64000)}};
  const auto chunks = ChunkSpeech(speech, {}, kFrame, Milliseconds{3200});
  ASSERT_EQ(chunks.size(), 2u);
  EXPECT_EQ(chunks[0].size(), 3u);
  EXPECT_EQ(chunks[1].size(), 1u);
  EXPECT_LE(JoinedSpeech(chunks[0]).size(), At(3200));
  EXPECT_TRUE(ChunkSpeech({}, {}, kFrame, Milliseconds{3200}).empty());
  // A little less: two and two.
  const auto pairs = ChunkSpeech(speech, {}, kFrame, Milliseconds{3100});
  ASSERT_EQ(pairs.size(), 2u);
  EXPECT_EQ(pairs[0].size(), 2u);
}

TEST(ChunkSpeechTest, CutsLongSpeechAtItsLeastLikelyFrameInTheSecondHalf) {
  // 10 s of speech in 4 s pieces: the first look is at 2-4 s, where frame
  // 100 (3.2 s) is least likely; a dip at 1 s, in the first half, is not
  // taken. Then 5.2-7.2 s with nothing lower, so at its last frame.
  std::vector<float> p(320, 0.9f);  // 10.24 s of frames
  p[31] = 0.1f;   // 0.99 s
  p[100] = 0.4f;  // 3.2 s
  const std::vector<SampleRange> speech = {{0, At(10000)}};
  const auto chunks = ChunkSpeech(speech, p, kFrame, Milliseconds{4000});
  ASSERT_EQ(chunks.size(), 3u);
  EXPECT_EQ(chunks[0], (std::vector<SampleRange>{{0, Frames(100)}}));
  EXPECT_EQ(chunks[1][0].begin, Frames(100));
  EXPECT_EQ(chunks[1][0].end, Frames(224));  // the last frame starting before 7.2 s
  EXPECT_EQ(chunks[2][0].end, At(10000));
  for (const auto& chunk : chunks) EXPECT_LE(JoinedSpeech(chunk).size(), At(4000));
}

TEST(ChunkSpeechTest, WhatIsLeftOfLongSpeechJoinsTheNextRegions) {
  // 5 s, cut just before 4 s, then the 1 s left and a 1 s region later in
  // one chunk.
  std::vector<float> p(400, 0.9f);  // 12.8 s of frames
  p[124] = 0.2f;  // 3.97 s
  const std::vector<SampleRange> speech = {{0, At(5000)}, {At(9000), At(10000)}};
  const auto chunks = ChunkSpeech(speech, p, kFrame, Milliseconds{4000});
  ASSERT_EQ(chunks.size(), 2u);
  EXPECT_EQ(chunks[0], (std::vector<SampleRange>{{0, Frames(124)}}));
  EXPECT_EQ(chunks[1], (std::vector<SampleRange>{{Frames(124), At(5000)}, {At(9000), At(10000)}}));
}

TEST(JoinedSpeechTest, MapsWordsBackToTheRecording) {
  // 1 s at 10 s, then 1 s at 20 s, 100 ms apart when joined.
  const std::vector<SampleRange> regions = {{160000, 176000}, {320000, 336000}};
  const JoinedSpeech joined(regions);
  EXPECT_EQ(joined.size(), 16000 + 1600 + 16000);
  EXPECT_EQ(joined.Offset(1), 17600);
  const auto at = [&](int start, int end) {
    const auto [first, last] = joined.ToRecording(Milliseconds{start}, Milliseconds{end});
    return std::pair<long long, long long>(first.count(), last.count());
  };
  EXPECT_EQ(at(200, 300), std::make_pair(10200LL, 10300LL));
  EXPECT_EQ(at(1200, 1300), std::make_pair(20100LL, 20200LL));
  // Starting in the gap: the next region.
  EXPECT_EQ(at(1050, 1300), std::make_pair(20000LL, 20200LL));
  // Running over the gap: stops where its region ends.
  EXPECT_EQ(at(900, 1300), std::make_pair(10900LL, 11000LL));
  // Past the end of the audio.
  EXPECT_EQ(at(2200, 2300), std::make_pair(21000LL, 21000LL));
}

}  // namespace
}  // namespace sillok
