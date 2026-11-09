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
  options.padding = Milliseconds{100};
  options.tail = Milliseconds{0};
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
  options.padding = Milliseconds{0};
  options.tail = Milliseconds{0};
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
  options.padding = Milliseconds{10};
  options.tail = Milliseconds{100};
  const auto speech = FindSpeech(Probabilities(300, {{100, 200}}), kFrame, Frames(300), options);
  ASSERT_EQ(speech.size(), 1u);
  EXPECT_EQ(speech[0].begin, Frames(100) - 160);
  EXPECT_EQ(speech[0].end, Frames(200) + 160 + 1600);
}

TEST(GroupSpeechTest, StartsAGroupAtLongPauses) {
  // 31 999 samples is under 2 s, 32 000 is not.
  const std::vector<SampleRange> speech = {
      {0, 16000}, {47999, 64000}, {96000, 112000}, {112001, 128000}};
  const auto groups = GroupSpeech(speech, Milliseconds{2000}, Milliseconds{180000});
  ASSERT_EQ(groups.size(), 2u);
  EXPECT_EQ(groups[0].size(), 2u);
  EXPECT_EQ(groups[1].size(), 2u);
  EXPECT_TRUE(GroupSpeech({}, Milliseconds{2000}, Milliseconds{180000}).empty());
}

TEST(GroupSpeechTest, SplitsLongGroupsAtTheirLongestPause) {
  // 0.8 s of speech each, 0.2 s apart but 0.7 s after 3.8 s and 0.8 s after
  // 7.3 s; 8.3 s joined.
  const auto at = [](int ms) { return ToSamples(Milliseconds{ms}); };
  std::vector<SampleRange> speech;
  for (const int start : {0, 1000, 2000, 3000, 4500, 5500, 6500, 8100, 9100}) {
    speech.push_back({at(start), at(start + 800)});
  }
  // At most 4 s: split at the 0.8 s pause, then the first part at 0.7 s.
  const auto groups = GroupSpeech(speech, Milliseconds{2000}, Milliseconds{4000});
  ASSERT_EQ(groups.size(), 3u);
  EXPECT_EQ(groups[0].size(), 4u);
  EXPECT_EQ(groups[1].size(), 3u);
  EXPECT_EQ(groups[2].size(), 2u);
  EXPECT_EQ(groups[2][0].begin, at(8100));
  // Speech without a pause is halved until short enough: 9 s in 2.25 s.
  const auto halves = GroupSpeech(std::vector<SampleRange>{{0, at(9000)}}, Milliseconds{2000},
                                  Milliseconds{4000});
  ASSERT_EQ(halves.size(), 4u);
  EXPECT_EQ(halves[1][0].begin, at(2250));
  EXPECT_EQ(halves[3][0].end, at(9000));
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
