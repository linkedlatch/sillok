#include <doctest/doctest.h>

#include "support.hpp"
#include "transcriber.hpp"

using namespace sillok;
using namespace sillok::testing;

static const std::vector<AudioChunk> chunks{{0, 10}, {12, 22}, {24, 34}};

static TranscriptionOptions optionsWith(std::vector<std::string> vocabulary) {
    TranscriptionOptions options;
    options.vocabulary = std::move(vocabulary);
    return options;
}

static std::vector<std::optional<std::string>> prompts(const FakeEngine& engine) {
    std::vector<std::optional<std::string>> result;
    for (const auto& options : engine.transcriptions) result.push_back(options.prompt);
    return result;
}

TEST_CASE("step carries previous text as prompt within the same language") {
    FakeEngine engine;
    engine.detectedLanguage = "ko";
    engine.nextSegments = {{plainSegment("첫 문장", 0, 5)}, {plainSegment("둘째 문장", 0, 5)}};
    ChunkTranscriber transcriber{optionsWith({"GOT"}), {}, {}};
    CancelToken cancel;

    auto first = transcriber.step(TranscriptionState{}, chunks, silentReader(), engine, cancel);
    auto second = transcriber.step(first.state, chunks, silentReader(), engine, cancel);

    CHECK(prompts(engine) == std::vector<std::optional<std::string>>{std::string("GOT"), std::string("GOT 첫 문장")});
    CHECK(second.state.nextChunk == 2);
    CHECK(first.segments[0].start == 0);
    CHECK(second.segments[0].start == 12);
    CHECK(second.state.previousText == std::optional<std::string>("둘째 문장"));
}

TEST_CASE("language change drops previous text") {
    FakeEngine engine;
    engine.detectedLanguage = "ko";
    engine.nextSegments = {{plainSegment("한국어", 0, 5)}};
    ChunkTranscriber transcriber{optionsWith({"GOT"}), {}, {}};
    CancelToken cancel;
    auto first = transcriber.step(TranscriptionState{}, chunks, silentReader(), engine, cancel);
    engine.detectedLanguage = "en";
    transcriber.step(first.state, chunks, silentReader(), engine, cancel);
    CHECK(prompts(engine) == std::vector<std::optional<std::string>>{std::string("GOT"), std::string("GOT")});
    CHECK(engine.transcriptions[0].language == "ko");
    CHECK(engine.transcriptions[1].language == "en");
}

TEST_CASE("low confidence chunk resets prompt context") {
    FakeEngine engine;
    engine.detectedLanguage = "ko";
    engine.nextSegments = {{plainSegment("불확실", 0, 5, -1.5f)}};
    ChunkTranscriber transcriber{TranscriptionOptions{}, {}, {}};
    CancelToken cancel;
    auto first = transcriber.step(TranscriptionState{}, chunks, silentReader(), engine, cancel);
    CHECK(first.segments[0].isLowConfidence);
    CHECK(!first.state.previousText);
    transcriber.step(first.state, chunks, silentReader(), engine, cancel);
    CHECK(prompts(engine) == std::vector<std::optional<std::string>>{std::nullopt, std::nullopt});
}

TEST_CASE("cancelled step throws before transcribing") {
    FakeEngine engine;
    CancelToken cancel;
    cancel.cancel();
    CHECK_THROWS_AS(ChunkTranscriber{}.step(TranscriptionState{}, chunks, silentReader(), engine, cancel), Cancelled);
    CHECK(engine.transcriptions.empty());
}

TEST_CASE("empty audio read is reported") {
    FakeEngine engine;
    CancelToken cancel;
    AudioReader empty = [](double, double) { return std::vector<float>{}; };
    CHECK_THROWS(ChunkTranscriber{}.step(TranscriptionState{}, chunks, empty, engine, cancel));
}
