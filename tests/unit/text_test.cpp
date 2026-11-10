#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "core/utf8.hpp"
#include "core/words.hpp"

namespace sillok {
namespace {

constexpr const char* kReplacement = "\xEF\xBF\xBD";


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

TEST(WordsTest, ChecksWordsFromModels) {
  std::vector<Word> words = {
      {.text = "early", .start = Milliseconds{-20}, .end = Milliseconds{100}, .speaker = std::nullopt},
      {.text = "", .start = Milliseconds{200}, .end = Milliseconds{300}, .speaker = std::nullopt},
      {.text = "same", .start = Milliseconds{400}, .end = Milliseconds{400}, .speaker = std::nullopt},
      {.text = "backwards", .start = Milliseconds{600}, .end = Milliseconds{500}, .speaker = std::nullopt},
      {.text = "late", .start = Milliseconds{900}, .end = Milliseconds{1200}, .speaker = std::nullopt}};
  words = CheckWords(std::move(words), Milliseconds{1000});
  ASSERT_EQ(words.size(), 4u);
  EXPECT_EQ(words[0].start, Milliseconds{0});
  EXPECT_EQ(words[1].text, "same");
  EXPECT_EQ(words[1].end, Milliseconds{400});
  EXPECT_EQ(words[2].start, Milliseconds{600});
  EXPECT_EQ(words[2].end, Milliseconds{600});
  EXPECT_EQ(words[3].end, Milliseconds{1000});
}

TEST(WordsTest, JoinsWordsByLanguage) {
  const std::vector<Word> words = {{.text = "日本", .start = {}, .end = {}, .speaker = std::nullopt},
                                   {.text = "語", .start = {}, .end = {}, .speaker = std::nullopt}};
  EXPECT_EQ(JoinWords(words, "ja"), "日本語");
  EXPECT_EQ(JoinWords(words, "en"), "日本 語");
}

}  // namespace
}  // namespace sillok
