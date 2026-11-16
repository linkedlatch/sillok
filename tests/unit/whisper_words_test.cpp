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
  const std::vector<Token> tokens = {T(" Hello", 1000, 1300), T(",", 1300, 1320),
                                     T(" wor", 1400, 1500), T("ld", 1500, 1600)};
  const auto words = AssembleWords(tokens, "en");
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
  const auto words = AssembleWords(tokens, "ko");
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
  const auto words = AssembleWords(tokens, "ko");
  ASSERT_EQ(words.size(), 1u);
  EXPECT_EQ(words[0].text, "사가");
}

TEST(WhisperWordsTest, LanguagesWithoutSpacesUseTokens) {
  const std::string hon = "本";  // E6 9C AC
  const std::vector<Token> tokens = {T("日" + hon.substr(0, 1), 0, 100),
                                     T(hon.substr(1), 100, 200),
                                     T("語", 200, 300)};
  const auto words = AssembleWords(tokens, "ja");
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0].text, "日本");
  EXPECT_EQ(words[1].text, "語");
  EXPECT_EQ(JoinWords(words, "ja"), "日本語");
}

TEST(WhisperWordsTest, OutputIsAlwaysValidUtf8) {
  const std::vector<Token> tokens = {T(" ok", 0, 100), T("\xFF", 100, 200),
                                     T(" \xEC\x95", 200, 300)};
  const auto words = AssembleWords(tokens, "en");
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0].text, std::string("ok") + kReplacement);
  EXPECT_EQ(words[1].text, kReplacement);
}

TEST(WhisperWordsTest, DropsSpaceOnlyWords) {
  const std::vector<Token> tokens = {T(" ", 0, 10), T(" a", 10, 20), T("b", 20, 30)};
  const auto words = AssembleWords(tokens, "en");
  ASSERT_EQ(words.size(), 1u);
  EXPECT_EQ(words[0].text, "ab");
  EXPECT_EQ(words[0].start, Milliseconds{10});
}

std::vector<std::string> Texts(const std::vector<Word>& words) {
  std::vector<std::string> texts;
  for (const Word& word : words) texts.push_back(word.text);
  return texts;
}

TEST(WhisperWordsTest, JapaneseKeepsTheSpacesWhisperPut) {
  const std::vector<Token> tokens = {T(" 機械", 0, 100), T("学習", 100, 200),
                                     T("の", 200, 300), T("Machine", 300, 400),
                                     T(" Learning", 400, 500), T("は", 500, 600)};
  const auto words = AssembleWords(tokens, "ja");
  EXPECT_EQ(Texts(words), (std::vector<std::string>{"機械", "学習", "の", "Machine",
                                                    " Learning", "は"}));
  EXPECT_EQ(JoinWords(words, "ja"), "機械学習のMachine Learningは");
}

TEST(WhisperWordsTest, ChineseKeepsOneSpaceWherePut) {
  // A space-only token and a trailing space put one space before what
  // follows, as in Qwen's words.
  const std::vector<Token> tokens = {T("我用", 0, 100), T(" Python", 100, 200),
                                     T(" ", 200, 210), T("写", 210, 300),
                                     T("代码 ", 300, 400), T("。", 400, 410)};
  const auto words = AssembleWords(tokens, "zh");
  EXPECT_EQ(Texts(words), (std::vector<std::string>{"我用", " Python", " 写", "代码",
                                                    " 。"}));
  EXPECT_EQ(words[2].start, Milliseconds{210});
  EXPECT_EQ(JoinWords(words, "zh"), "我用 Python 写代码 。");
}

TEST(WhisperWordsTest, LanguagesWithSpacesStayAsBefore) {
  const std::vector<Token> ko = {T(" 기계", 0, 100), T(" 학", 100, 200), T("습", 200, 300)};
  const auto ko_words = AssembleWords(ko, "ko");
  EXPECT_EQ(Texts(ko_words), (std::vector<std::string>{"기계", "학습"}));
  EXPECT_EQ(JoinWords(ko_words, "ko"), "기계 학습");
  const std::vector<Token> en = {T(" Machine", 0, 100), T(" ", 100, 110),
                                 T(" Learn", 110, 200), T("ing", 200, 300)};
  const auto en_words = AssembleWords(en, "en");
  EXPECT_EQ(Texts(en_words), (std::vector<std::string>{"Machine", "Learning"}));
  EXPECT_EQ(JoinWords(en_words, "en"), "Machine Learning");
}

TEST(WhisperWordsTest, JoinsTheGlossaryIntoOnePrompt) {
  const std::vector<std::string> glossary = {"sillok", "Whisper"};
  EXPECT_EQ(JoinGlossary(glossary), "sillok, Whisper");
  EXPECT_EQ(JoinGlossary({}), "");
}

}  // namespace
}  // namespace sillok
