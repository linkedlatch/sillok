#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/export.hpp"
#include "core/files.hpp"
#include "core/postprocess.hpp"
#include "core/sha256.hpp"
#include "core/speakers.hpp"
#include "core/utf8.hpp"
#include "core/words.hpp"

namespace sillok {
namespace {

Word W(std::string text, int start, int end) {
  return {std::move(text), Millis{start}, Millis{end}, 0.9f, std::nullopt};
}

Segment S(std::vector<Word> words, std::string language = "en") {
  return MakeSegment(std::move(words), std::move(language));
}

TEST(PostprocessTest, RemovesSegmentsDecodedFromSilence) {
  // 32 ms frames; speech only in the first second.
  std::vector<float> probabilities(100, 0.0f);
  std::fill(probabilities.begin(), probabilities.begin() + 31, 0.9f);
  std::vector<Segment> segments = {S({W("real", 100, 900)}),
                                   S({W("Thanks", 2000, 2500),
                                      W("for", 2500, 2700),
                                      W("watching", 2700, 3000)})};
  RemoveSilenceHallucinations(segments, probabilities, 512, {});
  ASSERT_EQ(segments.size(), 1u);
  EXPECT_EQ(segments[0].text, "real");
}

TEST(PostprocessTest, SplitsLongSegmentsAtSentenceEnd) {
  PostprocessOptions options;
  options.max_segment_duration = Millis{5000};
  const Segment long_segment = S({W("one", 0, 1000), W("two.", 1000, 2000),
                                  W("three", 2000, 3000), W("four,", 3500, 4000),
                                  W("five", 4100, 5000), W("six", 5000, 6000)});
  const auto out = SplitLongSegments({long_segment}, options);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].text, "one two.");
  EXPECT_EQ(out[1].text, "three four, five six");
  EXPECT_EQ(out[1].start, Millis{2000});
}

TEST(PostprocessTest, SplitsAtLongestPauseWithoutPunctuation) {
  PostprocessOptions options;
  options.max_segment_duration = Millis{4000};
  const Segment long_segment =
      S({W("a", 0, 500), W("b", 600, 1000), W("c", 1100, 1500),
         W("d", 2500, 3000), W("e", 3100, 3500), W("f", 3600, 4500)});
  const auto out = SplitLongSegments({long_segment}, options);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].text, "a b c");
}

TEST(PostprocessTest, SplitsByCharacterCount) {
  PostprocessOptions options;
  options.max_segment_characters = 10;
  const auto out = SplitLongSegments(
      {S({W("가나다", 0, 100), W("라마바", 100, 200), W("사아자", 200, 300),
          W("차카", 300, 400)}, "ko")},
      options);
  for (const Segment& segment : out) {
    EXPECT_LE(CountCodePoints(segment.text), 10u) << segment.text;
  }
}

TEST(SpeakersTest, MapsWordsSmoothsAndSplits) {
  const std::vector<Segment> segments = {
      S({W("hello", 0, 400), W("there", 500, 900), W("yes", 1000, 1200),
         W("so", 1300, 1500), W("anyway", 1600, 2000)}),
      S({W("right", 5000, 5500), W("indeed", 5600, 6000)})};
  // Speaker 7 talks except for a short "yes" by speaker 3 that is smoothed
  // away; speaker 3 takes over at 5 s.
  const std::vector<SpeakerTurn> turns = {
      {{0, ToSamples(Millis{950})}, 7},
      {{ToSamples(Millis{1000}), ToSamples(Millis{1200})}, 3},
      {{ToSamples(Millis{1250}), ToSamples(Millis{2100})}, 7},
      {{ToSamples(Millis{4900}), ToSamples(Millis{6100})}, 3}};
  const auto out = AssignSpeakers(segments, turns, {});
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].speaker, 0);  // renumbered by first appearance
  EXPECT_EQ(out[0].text, "hello there yes so anyway");
  EXPECT_EQ(out[1].speaker, 1);
}

TEST(SpeakersTest, SplitsSegmentAtSpeakerChange) {
  const std::vector<Segment> segments = {
      S({W("a", 0, 1000), W("b", 1000, 2000), W("c", 3000, 4000),
         W("d", 4000, 5000)})};
  const std::vector<SpeakerTurn> turns = {
      {{0, ToSamples(Millis{2000})}, 1},
      {{ToSamples(Millis{2900}), ToSamples(Millis{5000})}, 0}};
  const auto out = AssignSpeakers(segments, turns, {});
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].text, "a b");
  EXPECT_EQ(out[0].speaker, 0);
  EXPECT_EQ(out[1].text, "c d");
  EXPECT_EQ(out[1].speaker, 1);
  EXPECT_EQ(out[1].words[0].speaker, 1);
}

TEST(SpeakersTest, WordsBetweenTurnsTakeNeighbors) {
  const std::vector<Segment> segments = {
      S({W("a", 0, 1000), W("b", 3000, 4000), W("c", 6000, 7000)})};
  const std::vector<SpeakerTurn> turns = {
      {{0, ToSamples(Millis{1000})}, 4}, {{ToSamples(Millis{6000}), ToSamples(Millis{7000})}, 4}};
  const auto out = AssignSpeakers(segments, turns, {});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].words[1].speaker, 0);
}

Transcript TwoSpeakers() {
  Transcript transcript;
  Segment first = S({W("Hello", 0, 1500)});
  first.speaker = 0;
  Segment second = S({W("Hi", 61000, 3723456)});
  second.speaker = 1;
  transcript.segments = {first, second};
  return transcript;
}

TEST(ExportTest, TextLabelsOnlyWithSeveralSpeakers) {
  Transcript transcript = TwoSpeakers();
  const ExportOptions korean{.speaker_label = "화자 {n}"};
  EXPECT_EQ(ToText(transcript, korean), "화자 1: Hello\n화자 2: Hi\n");
  transcript.speaker_names[1] = "김교수";
  EXPECT_EQ(ToText(transcript, korean), "화자 1: Hello\n김교수: Hi\n");
  transcript.segments[1].speaker = 0;
  EXPECT_EQ(ToText(transcript, korean), "Hello\nHi\n");
}

TEST(ExportTest, Srt) {
  Transcript transcript = TwoSpeakers();
  transcript.segments[1].text = "Hi\n\nthere";  // edited by the app
  EXPECT_EQ(ToSrt(transcript, {}),
            "1\n00:00:00,000 --> 00:00:01,500\nSpeaker 1: Hello\n\n"
            "2\n00:01:01,000 --> 01:02:03,456\nSpeaker 2: Hi\nthere\n\n");
}

TEST(Sha256Test, KnownVectors) {
  const auto hex = [](std::string_view text) {
    Sha256 hash;
    hash.Update({reinterpret_cast<const unsigned char*>(text.data()), text.size()});
    return hash.HexDigest();
  };
  EXPECT_EQ(hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  EXPECT_EQ(hex(std::string(1000000, 'a')),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(FilesTest, ChecksSizeAndHashUnderUnicodePath) {
  const auto dir = std::filesystem::temp_directory_path() /
                   PathFromUtf8("sillok 테스트 모델");
  std::filesystem::create_directories(dir);
  const auto path = dir / PathFromUtf8("모델.bin");
  {
    std::ofstream out(path, std::ios::binary);
    out << "abc";
  }
  const std::string abc =
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  EXPECT_EQ(PathToUtf8(path.filename()), "모델.bin");
  EXPECT_EQ(CheckFile(path, 3, abc, true), FileCheck::kOk);
  EXPECT_EQ(CheckFile(path, 4, abc, true), FileCheck::kWrongSize);
  EXPECT_EQ(CheckFile(path, 3, std::string(64, '0'), false), FileCheck::kOk);
  EXPECT_EQ(CheckFile(path, 3, std::string(64, '0'), true), FileCheck::kWrongHash);
  EXPECT_EQ(CheckFile(dir / "missing", 3, abc, true), FileCheck::kMissing);
  std::filesystem::remove_all(dir);
}

}  // namespace
}  // namespace sillok
