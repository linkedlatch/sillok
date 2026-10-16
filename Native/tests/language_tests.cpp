#include <doctest/doctest.h>

#include "language.hpp"
#include "support.hpp"

using namespace sillok;
using namespace sillok::testing;

static std::vector<float> seconds(double value) {
    return std::vector<float>(static_cast<size_t>(value * kSampleRate), 0.0f);
}

static LanguageMode mode(LanguageMode::Kind kind, std::string code = "") {
    return LanguageMode{kind, std::move(code)};
}

TEST_CASE("fixed mode never detects") {
    FakeEngine engine;
    std::vector<AudioChunk> chunks{{0, 10}};
    CHECK(LanguageResolver{mode(LanguageMode::Kind::fixed, "ja")}.resolve(0, seconds(10), chunks, std::nullopt, silentReader(), engine) == "ja");
    CHECK(engine.detections.empty());
}

TEST_CASE("long chunk is detected with mode candidates") {
    for (auto kind : {LanguageMode::Kind::automatic, LanguageMode::Kind::koreanEnglish}) {
        FakeEngine engine;
        std::vector<AudioChunk> chunks{{0, 10}};
        CHECK(LanguageResolver{mode(kind)}.resolve(0, seconds(10), chunks, std::string("ko"), silentReader(), engine) == "en");
        REQUIRE(engine.detections.size() == 1);
        CHECK(engine.detections[0].second == mode(kind).allowedLanguages());
    }
}

TEST_CASE("short chunk inherits the previous language") {
    FakeEngine engine;
    std::vector<AudioChunk> chunks{{0, 10}, {12, 14}};
    CHECK(LanguageResolver{mode(LanguageMode::Kind::automatic)}.resolve(1, seconds(2), chunks, std::string("ko"), silentReader(), engine) == "ko");
    CHECK(engine.detections.empty());
}

TEST_CASE("first short chunk is detected together with following chunks") {
    FakeEngine engine;
    std::vector<AudioChunk> chunks{{0, 1}, {2, 3.5}, {5, 15}};
    CHECK(LanguageResolver{mode(LanguageMode::Kind::automatic)}.resolve(0, seconds(1), chunks, std::nullopt, silentReader(), engine) == "en");
    REQUIRE(engine.detections.size() == 1);
    CHECK(engine.detections[0].first == static_cast<size_t>(12.5 * kSampleRate));
}

TEST_CASE("audio shorter than three seconds falls back to Korean") {
    FakeEngine engine;
    std::vector<AudioChunk> chunks{{0, 1}, {1.5, 2.5}};
    CHECK(LanguageResolver{mode(LanguageMode::Kind::koreanEnglish)}.resolve(0, seconds(1), chunks, std::nullopt, silentReader(), engine) == "ko");
    CHECK(engine.detections.empty());
}

static std::pair<std::string, size_t> resolveMiddle(std::deque<LanguageDetection> scripted, std::optional<std::string> previous = std::string("ko")) {
    FakeEngine engine;
    engine.scriptedDetections = std::move(scripted);
    std::vector<AudioChunk> chunks{{0, 10}, {12, 22}, {24, 34}};
    auto language = LanguageResolver{mode(LanguageMode::Kind::koreanEnglish)}.resolve(1, seconds(10), chunks, previous, silentReader(), engine);
    return {language, engine.detections.size()};
}

TEST_CASE("low confidence switch keeps the previous language") {
    auto [language, detections] = resolveMiddle({{"en", 0.7}});
    CHECK(language == "ko");
    CHECK(detections == 1);
}

TEST_CASE("confident switch confirmed by the next chunk switches") {
    auto [language, detections] = resolveMiddle({{"en", 0.9}, {"en", 0.85}});
    CHECK(language == "en");
    CHECK(detections == 2);
}

TEST_CASE("confident switch not confirmed keeps the previous language") {
    CHECK(resolveMiddle({{"en", 0.95}, {"ko", 0.9}}).first == "ko");
    CHECK(resolveMiddle({{"en", 0.95}, {"en", 0.6}}).first == "ko");
}

TEST_CASE("first chunk accepts detection without confirmation") {
    auto [language, detections] = resolveMiddle({{"en", 0.55}}, std::nullopt);
    CHECK(language == "en");
    CHECK(detections == 1);
}

TEST_CASE("same language needs no confirmation") {
    auto [language, detections] = resolveMiddle({{"ko", 0.51}});
    CHECK(language == "ko");
    CHECK(detections == 1);
}

static int countWords(const std::string& text) {
    FakeEngine engine;
    return engine.tokenCount(text);
}

TEST_CASE("prompt combines terms and the tail of the previous text") {
    PromptBuilder builder({"GOT", "fprintf"}, 6);
    CHECK(builder.build(std::string("one two three four five six"), countWords) == std::optional<std::string>("GOT, fprintf three four five six"));
}

TEST_CASE("prompt without previous text is terms only") {
    CHECK(PromptBuilder({"GOT"}, 150).build(std::nullopt, countWords) == std::optional<std::string>("GOT"));
}

TEST_CASE("empty prompt is nullopt") {
    CHECK(!PromptBuilder({}, 150).build(std::nullopt, countWords));
}

TEST_CASE("terms beyond the limit are dropped from the end") {
    CHECK(PromptBuilder({"a", "b", "c", "d"}, 2).build(std::string("x y"), countWords) == std::optional<std::string>("a, b"));
}

TEST_CASE("max tokens is clamped to the whisper limit") {
    CHECK(PromptBuilder({}, 1000).maxTokens == 224);
}
