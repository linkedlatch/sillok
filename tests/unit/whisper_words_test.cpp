#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "backends/whisper/whisper_words.hpp"
#include "core/words.hpp"

namespace sillok {
namespace {

constexpr const char* kReplacement = "\xEF\xBF\xBD";

Token T(std::string text, int start_ms, int end_ms) {
  return {std::move(text), Milliseconds{start_ms}, Milliseconds{end_ms}};
}

TEST(WhisperWordsTest, AssemblesEnglishWords) {
  const std::vector<Token> tokens = {T(" Hello", 0, 300), T(",", 300, 320),
                                     T(" wor", 400, 500), T("ld", 500, 600)};
  const auto words = AssembleWords(tokens, Milliseconds{1000}, "en");
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0].text, "Hello,");
  EXPECT_EQ(words[0].start, Milliseconds{1000});
  EXPECT_EQ(words[0].end, Milliseconds{1320});
  EXPECT_EQ(words[1].text, "world");
  EXPECT_EQ(words[1].start, Milliseconds{1400});
  EXPECT_EQ(words[1].end, Milliseconds{1600});
  EXPECT_EQ(JoinWords(words, "en"), "Hello, world");
}

TEST(WhisperWordsTest, KeepsKoreanCharacterSplitAcrossTokens) {
  const std::string an = "안";  // EC 95 88
  const std::vector<Token> tokens = {T(" " + an.substr(0, 2), 0, 100),
                                     T(an.substr(2) + "녕", 100, 200),
                                     T(" 세계", 300, 400)};
  const auto words = AssembleWords(tokens, Milliseconds{0}, "ko");
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0].text, "안녕");
  EXPECT_EQ(words[0].end, Milliseconds{200});
  EXPECT_EQ(words[1].text, "세계");
}

TEST(WhisperWordsTest, CharacterPrefixWithoutSpaceContinuesWord) {
  const std::string ga = "가";  // EA B0 80
  const std::vector<Token> tokens = {T(" 사", 0, 100),
                                     T(ga.substr(0, 1), 100, 150),
                                     T(ga.substr(1), 150, 200)};
  const auto words = AssembleWords(tokens, Milliseconds{0}, "ko");
  ASSERT_EQ(words.size(), 1u);
  EXPECT_EQ(words[0].text, "사가");
}

TEST(WhisperWordsTest, LanguagesWithoutSpacesUseTokens) {
  const std::string hon = "本";  // E6 9C AC
  const std::vector<Token> tokens = {T("日" + hon.substr(0, 1), 0, 100),
                                     T(hon.substr(1), 100, 200),
                                     T("語", 200, 300)};
  const auto words = AssembleWords(tokens, Milliseconds{0}, "ja");
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0].text, "日本");
  EXPECT_EQ(words[1].text, "語");
  EXPECT_EQ(JoinWords(words, "ja"), "日本語");
}

TEST(WhisperWordsTest, OutputIsAlwaysValidUtf8) {
  const std::vector<Token> tokens = {T(" ok", 0, 100), T("\xFF", 100, 200),
                                     T(" \xEC\x95", 200, 300)};
  const auto words = AssembleWords(tokens, Milliseconds{0}, "en");
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0].text, std::string("ok") + kReplacement);
  EXPECT_EQ(words[1].text, kReplacement);
}

TEST(WhisperWordsTest, DropsSpaceOnlyWords) {
  const std::vector<Token> tokens = {T(" ", 0, 10), T(" a", 10, 20), T("b", 20, 30)};
  const auto words = AssembleWords(tokens, Milliseconds{0}, "en");
  ASSERT_EQ(words.size(), 1u);
  EXPECT_EQ(words[0].text, "ab");
  EXPECT_EQ(words[0].start, Milliseconds{10});
}

TEST(WhisperWordsTest, JoinsTheGlossaryIntoOnePrompt) {
  const std::vector<std::string> glossary = {"sillok", "Whisper"};
  EXPECT_EQ(JoinGlossary(glossary), "sillok, Whisper");
  EXPECT_EQ(JoinGlossary({}), "");
}

}  // namespace
}  // namespace sillok
