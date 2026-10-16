#include <doctest/doctest.h>

#include "sillok.h"
#include "support.hpp"

using namespace sillok;
using nlohmann::json;

TEST_CASE("language mode uses the swift codable layout") {
    CHECK(json(LanguageMode{LanguageMode::Kind::automatic, ""}).dump() == R"({"auto":{}})");
    CHECK(json(LanguageMode{LanguageMode::Kind::koreanEnglish, ""}).dump() == R"({"koreanEnglish":{}})");
    CHECK(json(LanguageMode{LanguageMode::Kind::fixed, "ja"}).dump() == R"({"fixed":{"_0":"ja"}})");
    auto parsed = json::parse(R"({"fixed":{"_0":"ja"}})").get<LanguageMode>();
    CHECK(parsed.kind == LanguageMode::Kind::fixed);
    CHECK(parsed.code == "ja");
}

TEST_CASE("segment round trips and omits missing optionals") {
    Segment segment;
    segment.id = "8A0B2E5C-1D3F-4A6B-9C7D-0E1F2A3B4C5D";
    segment.start = 1.5;
    segment.end = 3.25;
    segment.text = "안녕하세요";
    segment.language = "ko";
    segment.words = std::vector<WordTimestamp>{{"안녕하세요", 1.5, 3.25, 0.9f}};
    segment.isLowConfidence = true;
    json encoded = segment;
    CHECK(!encoded.contains("speaker"));
    CHECK(!encoded.contains("avgLogprob"));
    auto decoded = encoded.get<Segment>();
    CHECK(decoded.id == segment.id);
    CHECK(decoded.text == segment.text);
    CHECK(decoded.words->size() == 1);
    CHECK(decoded.isLowConfidence);
}

TEST_CASE("generated ids are uppercase uuids") {
    auto id = makeUUID();
    CHECK(id.size() == 36);
    CHECK(id[14] == '4');
    CHECK(id.find_first_of("abcdef") == std::string::npos);
}

TEST_CASE("state ignores segments and keeps optionals") {
    auto state = json::parse(R"({"nextChunk":3,"previousLanguage":"ko","segments":[]})").get<TranscriptionState>();
    CHECK(state.nextChunk == 3);
    CHECK(state.previousLanguage == std::optional<std::string>("ko"));
    CHECK(!state.previousText);
}

TEST_CASE("c api export wraps results in an envelope") {
    char* srt = sillok_export(R"([{"start":0.5,"end":2.25,"text":"안녕","language":"ko"}])", "srt");
    auto result = json::parse(srt);
    sillok_free(srt);
    CHECK(result["ok"] == true);
    CHECK(result["value"] == "1\n00:00:00,500 --> 00:00:02,250\n안녕\n");

    char* bad = sillok_export("[]", "doc");
    auto error = json::parse(bad);
    sillok_free(bad);
    CHECK(error["ok"] == false);
    CHECK(error["cancelled"] == false);
}

TEST_CASE("c api reports a missing model as an error") {
    sillok_engine* engine = sillok_engine_new();
    char* result = sillok_engine_load(engine, "/nonexistent/ggml-none.bin", nullptr, 1);
    CHECK(json::parse(result)["ok"] == false);
    sillok_free(result);
    char* transcription = sillok_engine_transcribe(engine, nullptr, 0, R"({"language":"en"})", nullptr);
    CHECK(json::parse(transcription)["ok"] == false);
    sillok_free(transcription);
    sillok_engine_free(engine);
}
