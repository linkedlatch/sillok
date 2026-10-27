#include <gtest/gtest.h>

#include <cstdlib>
#include <vector>

#include "core/segmenter.hpp"
#include "core/stitcher.hpp"

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
  options.min_silence = Millis{500};
  options.min_speech = Millis{250};
  options.padding = Millis{100};
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
  options.padding = Millis{0};
  std::vector<float> p = {0.1f, 0.6f, 0.4f, 0.4f, 0.3f, 0.1f};
  p.resize(20, 0.0f);
  std::fill(p.begin() + 1, p.begin() + 12, 0.4f);  // stays above 0.35
  p[1] = 0.6f;
  const auto speech = FindSpeech(p, kFrame, Frames(20), options);
  ASSERT_EQ(speech.size(), 1u);
  EXPECT_EQ(speech[0].begin, Frames(1));
  EXPECT_EQ(speech[0].end, Frames(12));
}

TEST(PlanWindowsTest, JoinsNearbySpeechAndTilesOwnership) {
  SegmenterOptions options;
  const SampleIndex total = ToSamples(Millis{60000});
  const std::vector<SampleRange> speech = {
      {ToSamples(Millis{1000}), ToSamples(Millis{5000})},
      {ToSamples(Millis{6000}), ToSamples(Millis{9000})},     // joins
      {ToSamples(Millis{20000}), ToSamples(Millis{25000})}};  // far: new window
  const std::vector<float> energy(6000, 1.0f);
  const auto windows = PlanWindows(speech, energy, 160, total, options);
  ASSERT_EQ(windows.size(), 2u);
  EXPECT_EQ(windows[0].audio, (SampleRange{ToSamples(Millis{1000}), ToSamples(Millis{9000})}));
  EXPECT_EQ(windows[1].audio, (SampleRange{ToSamples(Millis{20000}), ToSamples(Millis{25000})}));
  EXPECT_FALSE(windows[1].overlaps_previous);
  EXPECT_EQ(windows[0].owned.begin, 0);
  EXPECT_EQ(windows[0].owned.end, windows[1].owned.begin);
  EXPECT_EQ(windows[0].owned.end, ToSamples(Millis{14500}));
  EXPECT_EQ(windows[1].owned.end, total);
}

TEST(PlanWindowsTest, SplitsLongSpeechAtQuietestPointWithOverlap) {
  SegmenterOptions options;  // 28 s windows, 2 s overlap, 8 s search
  const SampleIndex total = ToSamples(Millis{70000});
  const std::vector<SampleRange> speech = {{0, total}};
  std::vector<float> energy(7000, 1.0f);  // 10 ms frames
  // V-shaped quiet spots centered on frames 2400 (24.0 s) and 4750 (47.5 s);
  // the one at 10 s lies outside the first search range.
  for (const int center : {1000, 2400, 4750}) {
    for (int i = center - 50; i <= center + 50; ++i) {
      energy[static_cast<std::size_t>(i)] = 0.01f * static_cast<float>(std::abs(i - center));
    }
  }
  const auto windows = PlanWindows(speech, energy, 160, total, options);
  ASSERT_EQ(windows.size(), 3u);
  const SampleIndex half = ToSamples(Millis{1000});
  EXPECT_EQ(windows[0].audio.begin, 0);
  EXPECT_NEAR(static_cast<double>(windows[0].audio.end - half),
              static_cast<double>(ToSamples(Millis{24005})), 160);
  EXPECT_TRUE(windows[1].overlaps_previous);
  EXPECT_EQ(windows[1].audio.begin + 2 * half, windows[0].audio.end);
  EXPECT_EQ(windows[0].owned.end, windows[1].audio.begin + half);
  EXPECT_NEAR(static_cast<double>(windows[1].owned.end),
              static_cast<double>(ToSamples(Millis{47505})), 160);
  EXPECT_EQ(windows[2].audio.end, total);
  for (const Window& window : windows) {
    EXPECT_LE(window.audio.length(), ToSamples(options.max_window));
    EXPECT_LE(window.audio.begin, window.owned.begin);
  }
}

Word W(std::string text, int start, int end) {
  return {std::move(text), Millis{start}, Millis{end}, 0.9f, std::nullopt};
}

TEST(StitchTest, JoinsAtSharedWordNearSeam) {
  // Overlap 9000-11000, seam 10000. Both windows heard "c d e" with
  // slightly different times; each word must survive exactly once.
  const std::vector<Word> earlier = {W("a", 8000, 8400), W("b", 8500, 8900),
                                     W("c", 9100, 9500), W("d", 9900, 10080),
                                     W("e", 10200, 10600), W("f", 10700, 10990)};
  const std::vector<Word> later = {W("C", 9060, 9480), W("d", 9950, 10120),
                                   W("e.", 10150, 10550), W("f", 10650, 10950),
                                   W("g", 11100, 11500)};
  const auto cut = FindStitch(earlier, later, Millis{9000}, Millis{11000}, Millis{10000});
  EXPECT_EQ(cut.earlier_keep, 4u);  // a b c d
  EXPECT_EQ(cut.later_skip, 2u);    // C d
}

TEST(StitchTest, MidpointRuleAvoidsDuplicateAndLossWhenTimesDisagree) {
  // Word "x" straddles the seam with times that put its midpoint on
  // different sides in the two windows; text still matches, so no
  // duplicate and no loss.
  const std::vector<Word> earlier = {W("p", 9200, 9600), W("x", 9800, 10150)};
  const std::vector<Word> later = {W("x", 9900, 10300), W("q", 10400, 10800)};
  const auto cut = FindStitch(earlier, later, Millis{9000}, Millis{11000}, Millis{10000});
  EXPECT_EQ(cut.earlier_keep + (later.size() - cut.later_skip), 3u);
}

TEST(StitchTest, FallsBackToMidpointsWithoutSharedWords) {
  const std::vector<Word> earlier = {W("a", 9000, 9500), W("b", 9800, 10400)};
  const std::vector<Word> later = {W("z", 9700, 9900), W("y", 10100, 10500)};
  const auto cut = FindStitch(earlier, later, Millis{9000}, Millis{11000}, Millis{10000});
  EXPECT_EQ(cut.earlier_keep, 1u);  // b's midpoint 10100 is past the seam
  EXPECT_EQ(cut.later_skip, 1u);    // z's midpoint 9800 is before it
}

TEST(StitchTest, TrimsSegments) {
  std::vector<Segment> segments(2);
  segments[0].words = {W("a", 0, 1), W("b", 1, 2)};
  segments[1].words = {W("c", 2, 3), W("d", 3, 4)};
  for (Segment& s : segments) s.language = "en";
  auto kept = segments;
  KeepLeadingWords(kept, 3);
  ASSERT_EQ(kept.size(), 2u);
  EXPECT_EQ(kept[1].text, "c");
  auto dropped = segments;
  DropLeadingWords(dropped, 3);
  ASSERT_EQ(dropped.size(), 1u);
  EXPECT_EQ(dropped[0].text, "d");
  EXPECT_EQ(dropped[0].start, Millis{3});
}

}  // namespace
}  // namespace sillok
