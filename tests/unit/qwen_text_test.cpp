#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "backends/qwen/qwen_text.hpp"

namespace sillok::qwen {
namespace {

// Expected values come from the official qwen-asr 0.0.6 package.

std::vector<std::string> Texts(const std::vector<AlignUnit>& units) {
  std::vector<std::string> texts;
  for (const AlignUnit& unit : units) texts.push_back(unit.text);
  return texts;
}

std::vector<Milliseconds> Times(std::initializer_list<int> ms) {
  std::vector<Milliseconds> times;
  for (const int value : ms) times.emplace_back(value);
  return times;
}

TEST(QwenTextTest, SplitsKoreanAsTheAlignerProcessor) {
  EXPECT_EQ(Texts(AlignUnits("안녕하세요, 여러분. 오늘은 머신러닝의 기초를 배워보겠습니다! "
                             "GPU가 없으면 3.14초 걸려요.",
                             "ko")),
            (std::vector<std::string>{"안녕", "하세요", "여러분", "오늘", "은", "머신", "러닝의",
                                      "기초", "를", "배워", "보겠습니다", "GPU가", "없으면",
                                      "314초", "걸려요"}));
  EXPECT_EQ(Texts(AlignUnits("그래서 우리는 '데이터' 를 정리했고요 — 결과는 좋았어요?", "ko")),
            (std::vector<std::string>{"그래서", "우리", "는", "'데이터'", "를", "정리", "했고요",
                                      "결과", "는", "좋았어요"}));
}

TEST(QwenTextTest, SplitsOtherLanguagesAtSpacesAndIdeographs) {
  EXPECT_EQ(Texts(AlignUnits("Hello, world! It's 3.14 o'clock; U.S.-made e-mail.", "en")),
            (std::vector<std::string>{"Hello", "world", "It's", "314", "o'clock", "USmade",
                                      "email"}));
  EXPECT_EQ(Texts(AlignUnits("我用Python写了一个程序，运行很快。Machine Learning 很有趣！", "zh")),
            (std::vector<std::string>{"我", "用", "Python", "写", "了", "一", "个", "程", "序",
                                      "运", "行", "很", "快", "Machine", "Learning", "很", "有",
                                      "趣"}));
  // Japanese as the other languages (the package tags it with nagisa).
  EXPECT_EQ(Texts(AlignUnits("今日はいい天気ですね。私はPythonを使います。", "ja")),
            (std::vector<std::string>{"今", "日", "はいい", "天", "気", "ですね", "私",
                                      "はPythonを", "使", "います"}));
}

TEST(QwenTextTest, UnitsKnowWhereTheyAre) {
  const auto units = AlignUnits("안녕하세요, 여러분.", "ko");
  ASSERT_EQ(units.size(), 3u);
  EXPECT_EQ(units[1].begin, 2u);
  EXPECT_EQ(units[1].end, 6u);  // with the comma
  EXPECT_EQ(units[2].begin, 7u);
}

TEST(QwenTextTest, FixesTimestampsAsThePackage) {
  EXPECT_EQ(FixTimestamps(std::vector<int>{30, 29, 80, 0, 33, 22, 26, 50, 29, 66, 85, 20}),
            (std::vector<int>{0, 0, 0, 0, 0, 22, 26, 50, 50, 66, 85, 85}));
  EXPECT_EQ(FixTimestamps(std::vector<int>{38, 15, 25, 23, 28, 12, 6, 13, 16, 11, 7, 87}),
            (std::vector<int>{15, 15, 25, 25, 28, 36, 44, 53, 61, 70, 78, 87}));
  EXPECT_EQ(FixTimestamps(std::vector<int>{35, 27, 69, 91, 26, 36, 27, 11, 52, 9, 98, 5}),
            (std::vector<int>{35, 35, 69, 91, 92, 93, 94, 95, 96, 97, 98, 98}));
  EXPECT_TRUE(FixTimestamps(std::vector<int>{}).empty());
}

TEST(QwenTextTest, CutsRepetitions) {
  std::string text;
  for (int i = 0; i < 25; ++i) text += "아";
  text += "네 ";
  for (int i = 0; i < 22; ++i) text += "좋아요 ";
  text += "끝";
  EXPECT_EQ(FixRepetitions(text), "아네 좋아요 끝");
  EXPECT_EQ(FixRepetitions("짧은 글"), "짧은 글");
}

TEST(QwenTextTest, ParsesTheModelOutput) {
  const AsrOutput told = ParseAsrOutput("language Korean<asr_text>안녕하세요.", "");
  EXPECT_EQ(told.language, "ko");
  EXPECT_EQ(told.text, "안녕하세요.");
  const AsrOutput none = ParseAsrOutput("language None<asr_text>", "");
  EXPECT_EQ(none.language, "");
  EXPECT_EQ(none.text, "");
  const AsrOutput lines = ParseAsrOutput("  language english\n<asr_text> Hi there ", "");
  EXPECT_EQ(lines.language, "en");
  EXPECT_EQ(lines.text, "Hi there");
  EXPECT_EQ(ParseAsrOutput("plain text", "").language, "");
  EXPECT_EQ(ParseAsrOutput("language Thai<asr_text>x", "").language, "");  // not sillok's
  const AsrOutput given = ParseAsrOutput("Hello.", "en");
  EXPECT_EQ(given.language, "en");
  EXPECT_EQ(given.text, "Hello.");
}

TEST(QwenTextTest, NamesLanguages) {
  EXPECT_EQ(LanguageName("yue"), "Cantonese");
  EXPECT_EQ(LanguageCode("Korean"), "ko");
  EXPECT_EQ(LanguageName("vi"), "");
}

TEST(QwenTextTest, WordsWithSpacesKeepTheirPunctuation) {
  const std::string text = "— 안녕하세요, 여러분. !";
  const auto units = AlignUnits(text, "ko");
  ASSERT_EQ(units.size(), 3u);
  const auto words = TimedWords(text, "ko", units, Times({100, 200, 200, 400, 500, 900}));
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0].text, "— 안녕하세요,");
  EXPECT_EQ(words[0].start, Milliseconds{100});
  EXPECT_EQ(words[0].end, Milliseconds{400});
  EXPECT_EQ(words[1].text, "여러분. !");
  EXPECT_EQ(words[1].start, Milliseconds{500});
  EXPECT_EQ(words[1].end, Milliseconds{900});
}

TEST(QwenTextTest, WordsWithoutSpacesAreUnits) {
  const std::string text = "「我用 Machine Learning，很好！」";
  const auto units = AlignUnits(text, "zh");
  ASSERT_EQ(units.size(), 6u);
  const auto words = TimedWords(text, "zh", units,
                                Times({0, 10, 10, 20, 30, 40, 40, 50, 60, 70, 70, 80}));
  std::string joined;
  std::vector<std::string> texts;
  for (const Word& word : words) {
    texts.push_back(word.text);
    joined += word.text;
  }
  EXPECT_EQ(texts, (std::vector<std::string>{"「我", "用", " Machine", " Learning，", "很", "好！」"}));
  EXPECT_EQ(joined, text);
  EXPECT_EQ(words[2].start, Milliseconds{30});
  EXPECT_EQ(words[5].end, Milliseconds{80});
}

TEST(QwenTextTest, TextWithoutLettersHasNoWords) {
  EXPECT_TRUE(AlignUnits("… — !", "en").empty());
  EXPECT_TRUE(TimedWords("… — !", "en", {}, {}).empty());
}

TEST(QwenTextTest, CountsAudioTokensAsTheEncoder) {
  EXPECT_EQ(AudioTokens(180 * 16000), 2340u);
  EXPECT_EQ(AudioTokens(0), 0u);
  EXPECT_EQ(AudioTokens(16000), 13u);
  EXPECT_EQ(AudioTokens(16000 + 160 * 50), 13u + 7u);
}

}  // namespace
}  // namespace sillok::qwen
