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
  return {std::move(text), Milliseconds{start}, Milliseconds{end}, std::nullopt};
}

Segment S(std::vector<Word> words, std::string language = "en") {
  return MakeSegment(std::move(words), std::move(language));
}

TEST(PostprocessTest, KnowsSentenceEndsOfOtherScripts) {
  // Hindi's danda, Arabic's question mark, a CJK full stop; not a comma or a
  // closing quote.
  const auto out = SplitSegments(
      {S({W("\xE0\xA4\xB9\xE0\xA5\xA4", 0, 300), W("\xD8\x9F", 300, 400),
          W("\xE3\x80\x82", 400, 500), W("a,", 500, 600), W("b\"", 600, 700),
          W("c", 700, 800)})},
      {});
  ASSERT_EQ(out.size(), 4u);
  EXPECT_EQ(out[3].text, "a, b\" c");
}

TEST(PostprocessTest, SplitsSegmentsIntoSentences) {
  // Short enough to read, yet three sentences: one each.
  const auto out = SplitSegments(
      {S({W("Okay.", 0, 300), W("Is", 400, 500), W("it?", 500, 700),
          W("Yes", 800, 900), W("it", 900, 1000), W("is.", 1000, 1200)})},
      {});
  ASSERT_EQ(out.size(), 3u);
  EXPECT_EQ(out[0].text, "Okay.");
  EXPECT_EQ(out[1].text, "Is it?");
  EXPECT_EQ(out[2].text, "Yes it is.");
  EXPECT_EQ(out[2].start, Milliseconds{800});
}

TEST(PostprocessTest, SplitsLongSentencesAtTheLongestPause) {
  PostprocessOptions options;
  options.max_segment_duration = Milliseconds{4000};
  // A comma is no place to split; the pause after "c" is.
  const Segment long_segment =
      S({W("a,", 0, 500), W("b", 500, 1000), W("c", 1000, 1500),
         W("d", 2500, 3000), W("e", 3000, 3500), W("f", 3600, 4500)});
  const auto out = SplitSegments({long_segment}, options);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].text, "a, b c");
  EXPECT_EQ(out[1].text, "d e f");
}

TEST(PostprocessTest, SplitsLongSentencesWithoutPausesNearTheMiddle) {
  PostprocessOptions options;
  options.max_segment_duration = Milliseconds{4000};
  // Middle at 2600 ms: "c" starts nearest it.
  const Segment long_segment = S({W("a", 200, 1000), W("b", 1000, 2500),
                                  W("c", 2500, 3000), W("d", 3000, 5000)});
  const auto out = SplitSegments({long_segment}, options);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].text, "a b");
  EXPECT_EQ(out[1].text, "c d");
}

TEST(PostprocessTest, HalvesUntilShortEnough) {
  PostprocessOptions options;
  options.max_segment_duration = Milliseconds{2000};
  std::vector<Word> words;
  for (int i = 0; i < 8; ++i) {
    words.push_back(W("w" + std::to_string(i), i * 1000, (i + 1) * 1000));
  }
  const auto out = SplitSegments({S(std::move(words))}, options);
  ASSERT_EQ(out.size(), 4u);
  for (const Segment& segment : out) {
    EXPECT_LE(segment.end - segment.start, Milliseconds{2000}) << segment.text;
  }
  EXPECT_EQ(out[0].text, "w0 w1");
}

TEST(SpeakersTest, MapsWordsAndKeepsShortInterjections) {
  const std::vector<Segment> segments = {
      S({W("hello", 0, 400), W("there", 500, 900), W("yes", 1000, 1200),
         W("so", 1300, 1500), W("anyway", 1600, 2000)}),
      S({W("right", 5000, 5500), W("indeed", 5600, 6000)})};
  // Speaker 7 talks except for a short "yes" by speaker 3, who takes over
  // at 5 s.
  const std::vector<SpeakerTurn> turns = {
      {{0, ToSamples(Milliseconds{950})}, 7},
      {{ToSamples(Milliseconds{1000}), ToSamples(Milliseconds{1200})}, 3},
      {{ToSamples(Milliseconds{1250}), ToSamples(Milliseconds{2100})}, 7},
      {{ToSamples(Milliseconds{4900}), ToSamples(Milliseconds{6100})}, 3}};
  const auto out = AssignSpeakers(segments, turns);
  ASSERT_EQ(out.size(), 4u);
  EXPECT_EQ(out[0].speaker, 0);  // renumbered by first appearance
  EXPECT_EQ(out[0].text, "hello there");
  EXPECT_EQ(out[1].speaker, 1);
  EXPECT_EQ(out[1].text, "yes");
  EXPECT_EQ(out[2].speaker, 0);
  EXPECT_EQ(out[2].text, "so anyway");
  EXPECT_EQ(out[3].speaker, 1);
}

TEST(SpeakersTest, SplitsSegmentAtSpeakerChange) {
  const std::vector<Segment> segments = {
      S({W("a", 0, 1000), W("b", 1000, 2000), W("c", 3000, 4000),
         W("d", 4000, 5000)})};
  const std::vector<SpeakerTurn> turns = {
      {{0, ToSamples(Milliseconds{2000})}, 1},
      {{ToSamples(Milliseconds{2900}), ToSamples(Milliseconds{5000})}, 0}};
  const auto out = AssignSpeakers(segments, turns);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].text, "a b");
  EXPECT_EQ(out[0].speaker, 0);
  EXPECT_EQ(out[1].text, "c d");
  EXPECT_EQ(out[1].speaker, 1);
  EXPECT_EQ(out[1].words[0].speaker, 1);
}

TEST(SpeakersTest, WordsBetweenTurnsTakeTheNearest) {
  const std::vector<Segment> segments = {
      S({W("a", 0, 1000), W("b", 3000, 4000), W("c", 6000, 7000)})};
  const std::vector<SpeakerTurn> turns = {
      {{0, ToSamples(Milliseconds{1000})}, 4}, {{ToSamples(Milliseconds{6000}), ToSamples(Milliseconds{7000})}, 4}};
  const auto out = AssignSpeakers(segments, turns);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].words[1].speaker, 0);

  // However far, and from either side: "b" is closer to the turn after it.
  const std::vector<SpeakerTurn> apart = {
      {{0, ToSamples(Milliseconds{1000})}, 4},
      {{ToSamples(Milliseconds{4500}), ToSamples(Milliseconds{7000})}, 2}};
  const auto split = AssignSpeakers(segments, apart);
  ASSERT_EQ(split.size(), 2u);
  EXPECT_EQ(split[0].text, "a");
  EXPECT_EQ(split[1].text, "b c");
  EXPECT_EQ(split[1].speaker, 1);
}

TEST(SpeakersTest, NoTurnsLeaveWordsWithoutSpeakers) {
  const auto out = AssignSpeakers({S({W("a", 0, 1000), W("b", 1000, 2000)})}, {});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_FALSE(out[0].speaker.has_value());
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
