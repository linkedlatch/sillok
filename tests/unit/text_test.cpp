#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/utf8.hpp"
#include "core/words.hpp"

namespace sillok {
namespace {

constexpr const char* kReplacement = "\xEF\xBF\xBD";

Token T(std::string text, int start_ms, int end_ms, float p = 0.8f) {
  return {std::move(text), Millis{start_ms}, Millis{end_ms}, p};
}

TEST(Utf8Test, KeepsValidText) {
  const std::string text = "Hello 안녕하세요 日本語 😀";
  EXPECT_EQ(SanitizeUtf8(text), text);
}

TEST(Utf8Test, ReplacesInvalidSequences) {
  EXPECT_EQ(SanitizeUtf8("a\xFF" "b"), std::string("a") + kReplacement + "b");
  // Overlong encoding and a lone continuation byte.
  EXPECT_EQ(SanitizeUtf8("\xC0\xAF"), std::string(kReplacement) + kReplacement);
  EXPECT_EQ(SanitizeUtf8("\x80"), kReplacement);
  // Surrogate code point and a truncated sequence followed by ASCII.
  EXPECT_EQ(SanitizeUtf8("\xED\xA0\x80"),
            std::string(kReplacement) + kReplacement + kReplacement);
  EXPECT_EQ(SanitizeUtf8("\xEC\x95" "x"), std::string(kReplacement) + "x");
}

TEST(Utf8Test, FindsIncompleteTail) {
  const std::string an = "안";  // EC 95 88
  EXPECT_EQ(IncompleteUtf8Tail("abc"), 0u);
  EXPECT_EQ(IncompleteUtf8Tail(an), 0u);
  EXPECT_EQ(IncompleteUtf8Tail("x" + an.substr(0, 1)), 1u);
  EXPECT_EQ(IncompleteUtf8Tail("x" + an.substr(0, 2)), 2u);
  EXPECT_EQ(IncompleteUtf8Tail("\xF0\x9F\x98"), 3u);
  EXPECT_EQ(IncompleteUtf8Tail("x\xFF"), 0u);  // invalid, not incomplete
}

TEST(Utf8Test, CountsCodePoints) {
  EXPECT_EQ(CountCodePoints("a안b😀"), 4u);
}

TEST(WordsTest, AssemblesEnglishWords) {
  const std::vector<Token> tokens = {T(" Hello", 0, 300), T(",", 300, 320),
                                     T(" wor", 400, 500), T("ld", 500, 600)};
  const auto words = AssembleWords(tokens, Millis{1000}, "en");
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0].text, "Hello,");
  EXPECT_EQ(words[0].start, Millis{1000});
  EXPECT_EQ(words[0].end, Millis{1320});
  EXPECT_EQ(words[1].text, "world");
  EXPECT_EQ(words[1].start, Millis{1400});
  EXPECT_EQ(words[1].end, Millis{1600});
  EXPECT_EQ(JoinWords(words, "en"), "Hello, world");
}

TEST(WordsTest, KeepsKoreanCharacterSplitAcrossTokens) {
  const std::string an = "안";  // EC 95 88
  const std::vector<Token> tokens = {T(" " + an.substr(0, 2), 0, 100),
                                     T(an.substr(2) + "녕", 100, 200),
                                     T(" 세계", 300, 400)};
  const auto words = AssembleWords(tokens, Millis{0}, "ko");
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0].text, "안녕");
  EXPECT_EQ(words[0].end, Millis{200});
  EXPECT_EQ(words[1].text, "세계");
}

TEST(WordsTest, CharacterPrefixWithoutSpaceContinuesWord) {
  const std::string ga = "가";  // EA B0 80
  const std::vector<Token> tokens = {T(" 사", 0, 100),
                                     T(ga.substr(0, 1), 100, 150),
                                     T(ga.substr(1), 150, 200)};
  const auto words = AssembleWords(tokens, Millis{0}, "ko");
  ASSERT_EQ(words.size(), 1u);
  EXPECT_EQ(words[0].text, "사가");
}

TEST(WordsTest, LanguagesWithoutSpacesUseTokens) {
  const std::string hon = "本";  // E6 9C AC
  const std::vector<Token> tokens = {T("日" + hon.substr(0, 1), 0, 100),
                                     T(hon.substr(1), 100, 200),
                                     T("語", 200, 300)};
  const auto words = AssembleWords(tokens, Millis{0}, "ja");
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0].text, "日本");
  EXPECT_EQ(words[1].text, "語");
  EXPECT_EQ(JoinWords(words, "ja"), "日本語");
}

TEST(WordsTest, OutputIsAlwaysValidUtf8) {
  const std::vector<Token> tokens = {T(" ok", 0, 100), T("\xFF", 100, 200),
                                     T(" \xEC\x95", 200, 300)};
  const auto words = AssembleWords(tokens, Millis{0}, "en");
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0].text, std::string("ok") + kReplacement);
  EXPECT_EQ(words[1].text, kReplacement);
}

TEST(WordsTest, DropsSpaceOnlyWordsAndAveragesConfidence) {
  const std::vector<Token> tokens = {T(" ", 0, 10, 0.1f), T(" a", 10, 20, 0.4f),
                                     T("b", 20, 30, 0.8f)};
  const auto words = AssembleWords(tokens, Millis{0}, "en");
  ASSERT_EQ(words.size(), 1u);
  EXPECT_EQ(words[0].text, "ab");
  EXPECT_FLOAT_EQ(words[0].confidence, 0.6f);
}

}  // namespace
}  // namespace sillok
