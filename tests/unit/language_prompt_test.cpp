#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/language.hpp"
#include "core/prompt.hpp"

namespace sillok {
namespace {

std::vector<LanguageScore> Scores(float ko, float en, float ja = 0.0f) {
  return {{"ko", ko}, {"en", en}, {"ja", ja}};
}

TEST(LanguageStabilizerTest, RestrictsToCandidates) {
  LanguageStabilizer stabilizer({"ko", "en"}, {});
  // Japanese wins overall but is not a candidate: Korean wins among ko/en.
  const auto decision = stabilizer.Decide(Scores(0.3f, 0.1f, 0.6f));
  EXPECT_EQ(decision.language, "ko");
}

TEST(LanguageStabilizerTest, KeepsLanguageAgainstWeakChallenger) {
  LanguageStabilizer stabilizer({"ko", "en"}, {});
  stabilizer.Commit(stabilizer.Decide(Scores(0.9f, 0.1f)).language);
  const auto decision = stabilizer.Decide(Scores(0.45f, 0.55f));
  EXPECT_EQ(decision.language, "ko");
  EXPECT_TRUE(decision.overruled);
  EXPECT_EQ(decision.alternative, "en");
  stabilizer.Commit("ko");
  // A weak challenger does not accumulate towards a switch.
  EXPECT_EQ(stabilizer.Decide(Scores(0.45f, 0.55f)).language, "ko");
}

TEST(LanguageStabilizerTest, SwitchesAtOnceOnStrongEvidence) {
  LanguageStabilizer stabilizer({"ko", "en"}, {});
  stabilizer.Commit(stabilizer.Decide(Scores(0.9f, 0.1f)).language);
  const auto decision = stabilizer.Decide(Scores(0.1f, 0.9f));
  EXPECT_EQ(decision.language, "en");
  EXPECT_FALSE(decision.overruled);
  EXPECT_FALSE(decision.alternative.has_value());
}

TEST(LanguageStabilizerTest, SwitchesWhenTwoWindowsAgree) {
  LanguageStabilizer stabilizer({"ko", "en"}, {});
  stabilizer.Commit(stabilizer.Decide(Scores(0.9f, 0.1f)).language);
  auto decision = stabilizer.Decide(Scores(0.3f, 0.7f));
  EXPECT_EQ(decision.language, "ko");
  stabilizer.Commit(decision.language);
  decision = stabilizer.Decide(Scores(0.3f, 0.7f));
  EXPECT_EQ(decision.language, "en");
}

TEST(LanguageStabilizerTest, CommittedRetryChangesLanguage) {
  LanguageStabilizer stabilizer({"ko", "en"}, {});
  stabilizer.Commit(stabilizer.Decide(Scores(0.9f, 0.1f)).language);
  const auto decision = stabilizer.Decide(Scores(0.35f, 0.65f));
  ASSERT_EQ(decision.alternative, "en");
  stabilizer.Commit("en");  // the retry in English decoded better
  EXPECT_EQ(stabilizer.Decide(Scores(0.4f, 0.6f)).language, "en");
}

TEST(LanguageStabilizerTest, OffersAlternativeOnlyWhenUnsure) {
  LanguageStabilizer stabilizer({"ko", "en"}, {});
  EXPECT_FALSE(stabilizer.Decide(Scores(0.95f, 0.05f)).alternative.has_value());
  EXPECT_EQ(stabilizer.Decide(Scores(0.7f, 0.3f)).alternative, "en");
  EXPECT_FALSE(stabilizer.Decide(Scores(0.85f, 0.15f)).alternative.has_value());
}

// One token per space-separated word, like a crude tokenizer.
int CountWords(std::string_view text) {
  int words = 0;
  bool in_word = false;
  for (const char c : text) {
    if (c != ' ' && !in_word) ++words;
    in_word = c != ' ';
  }
  return words;
}

TEST(PromptBuilderTest, PutsGlossaryFirstAndKeepsRecentContext) {
  const PromptBuilder builder({"Kubernetes", "latency"}, 6, CountWords);
  EXPECT_EQ(builder.Build("one two three four five six", "en"),
            "Kubernetes, latency three four five six");
}

TEST(PromptBuilderTest, SkipsGlossaryTermsThatDoNotFit) {
  const PromptBuilder builder({"alpha", "beta gamma", "delta"}, 2, CountWords);
  EXPECT_EQ(builder.Build("", "en"), "alpha, delta");
}

TEST(PromptBuilderTest, ContextOnlyWithoutGlossary) {
  const PromptBuilder builder({}, 2, CountWords);
  EXPECT_EQ(builder.Build("a b c", "en"), "b c");
  EXPECT_EQ(builder.Build("", "en"), "");
}

TEST(PromptBuilderTest, CutsAtCharactersInLanguagesWithoutSpaces) {
  // Count bytes / 3: one token per Japanese character.
  const auto count = [](std::string_view text) {
    return static_cast<int>(text.size() / 3);
  };
  const PromptBuilder builder({}, 2, count);
  EXPECT_EQ(builder.Build("日本語です", "ja"), "です");
}

}  // namespace
}  // namespace sillok
