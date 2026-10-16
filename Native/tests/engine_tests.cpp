#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <thread>

#include "chunking.hpp"
#include "support.hpp"
#include "transcriber.hpp"
#include "whisper_engine.hpp"

using namespace sillok;
using namespace sillok::testing;

static bool missing(std::initializer_list<std::filesystem::path> paths) {
    for (const auto& path : paths) {
        if (!std::filesystem::exists(path)) return true;
    }
    return false;
}

static std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

TEST_CASE("core ml encoder name follows the whisper rule") {
    CHECK(coreMLEncoderName("ggml-large-v3-turbo-q8_0.bin") == "ggml-large-v3-turbo-encoder.mlmodelc");
    CHECK(coreMLEncoderName("ggml-large-v3-turbo-q5_0.bin") == "ggml-large-v3-turbo-encoder.mlmodelc");
    CHECK(coreMLEncoderName("ggml-large-v3-q5_0.bin") == "ggml-large-v3-encoder.mlmodelc");
    CHECK(coreMLEncoderName("ggml-small-q5_1.bin") == "ggml-small-encoder.mlmodelc");
    CHECK(coreMLEncoderName("ggml-large-v3-turbo.bin") == "ggml-large-v3-turbo-encoder.mlmodelc");
}

TEST_CASE("compression ratio flags repetition") {
    auto normal = compressionRatio("In today's lecture we will look at how format string bugs happen, and explain step by step how an attacker can use this vulnerability.");
    std::string repeated;
    for (int i = 0; i < 40; ++i) repeated += "감사합니다. ";
    auto repetitive = compressionRatio(repeated);
    REQUIRE(normal);
    REQUIRE(repetitive);
    CHECK(*normal < 2.4f);
    CHECK(*repetitive > 2.4f);
    CHECK(!compressionRatio(""));
}

TEST_CASE("tokens join into words at a leading space") {
    auto words = assembleWords({{" The", 0, 0.2, 0.8f}, {" quick", 0.2, 0.5, 0.8f}, {" bro", 0.5, 0.6, 0.8f}, {"wn", 0.6, 0.8, 0.8f}, {".", 0.8, 0.85, 0.8f}});
    REQUIRE(words.size() == 3);
    CHECK(words[2].word == " brown.");
    CHECK(words[2].start == doctest::Approx(0.5));
    CHECK(words[2].end == doctest::Approx(0.85));
}

TEST_CASE("korean character split across tokens is decoded whole") {
    std::string bytes = " 안녕";
    auto words = assembleWords({{bytes.substr(0, 3), 1.0, 1.1, 0.6f}, {bytes.substr(3), 1.1, 1.4, 1.0f}, {" 하세요", 1.4, 1.9, 0.8f}});
    REQUIRE(words.size() == 2);
    CHECK(words[0].word == " 안녕");
    CHECK(words[0].probability == doctest::Approx(0.8f));
    CHECK(words[1].word == " 하세요");
}

TEST_SUITE("model") {

TEST_CASE("turbo transcribes english speech with ordered words" * doctest::skip(missing({modelPath(testModel), fixturePath("en.wav")}))) {
    auto samples = readWav(fixturePath("en.wav"));
    WhisperEngine engine;
    engine.load(modelPath(testModel), std::nullopt, true);
    CancelToken cancel;
    auto segments = engine.transcribe(samples, ChunkTranscribeOptions{"en", std::nullopt, 5}, cancel);
    std::string text;
    for (const auto& segment : segments) {
        text += lowercase(segment.text) + " ";
        REQUIRE(segment.words);
        CHECK(segment.avgLogprob);
        CHECK(segment.noSpeechProb);
        for (size_t i = 1; i < segment.words->size(); ++i) CHECK((*segment.words)[i - 1].start <= (*segment.words)[i].start);
    }
    CHECK(text.find("fox") != std::string::npos);
    CHECK(text.find("dog") != std::string::npos);
}

TEST_CASE("turbo detects korean and english" * doctest::skip(missing({modelPath(testModel), fixturePath("ko.wav"), fixturePath("en.wav")}))) {
    WhisperEngine engine;
    engine.load(modelPath(testModel), std::nullopt, true);
    for (auto [file, expected] : {std::pair{"ko.wav", "ko"}, std::pair{"en.wav", "en"}}) {
        auto samples = readWav(fixturePath(file));
        auto any = engine.detectLanguage(samples, std::nullopt);
        auto restricted = engine.detectLanguage(samples, std::set<std::string>{"ko", "en"});
        CHECK(any.language == expected);
        CHECK(restricted.language == expected);
        CHECK(restricted.confidence >= any.confidence);
    }
    CHECK(engine.tokenCount("hello") > 0);
}

TEST_CASE("cancellation stops inference within one encoder pass" * doctest::skip(missing({modelPath(testModel), fixturePath("en.wav")}))) {
    auto sentence = readWav(fixturePath("en.wav"));
    std::vector<float> samples;
    for (int i = 0; i < 30; ++i) samples.insert(samples.end(), sentence.begin(), sentence.end());
    WhisperEngine engine;
    engine.load(modelPath(testModel), std::nullopt, true);
    CancelToken cancel;
    std::chrono::steady_clock::time_point cancelledAt;
    std::thread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        cancelledAt = std::chrono::steady_clock::now();
        cancel.cancel();
    });
    CHECK_THROWS_AS(engine.transcribe(samples, ChunkTranscribeOptions{"en", std::nullopt, 5}, cancel), Cancelled);
    auto elapsed = std::chrono::steady_clock::now() - cancelledAt;
    canceller.join();
    CHECK(elapsed < std::chrono::seconds(3));
}

TEST_CASE("vad plans speech between silences as one chunk" * doctest::skip(missing({modelPath(vadModel), fixturePath("en.wav")}))) {
    auto speech = readWav(fixturePath("en.wav"));
    std::vector<float> samples(3 * kSampleRate, 0.0f);
    samples.insert(samples.end(), speech.begin(), speech.end());
    samples.insert(samples.end(), 3 * kSampleRate, 0.0f);
    VoiceActivityDetector vad(modelPath(vadModel));
    SpeechMapBuilder builder(vad);
    const size_t window = VoiceActivityDetector::frameSize * 50;
    for (size_t offset = 0; offset < samples.size(); offset += window) {
        builder.append(std::vector<float>(samples.begin() + offset, samples.begin() + std::min(offset + window, samples.size())));
    }
    auto map = builder.finish(samples.size() / static_cast<double>(kSampleRate));
    CHECK(map.probabilities.size() == (samples.size() + 511) / 512);
    auto chunks = ChunkPlanner{}.plan(map);
    REQUIRE(chunks.size() == 1);
    CHECK(std::abs(chunks[0].start - 3) < 0.5);
    CHECK(std::abs(chunks[0].end - (3 + speech.size() / static_cast<double>(kSampleRate))) < 0.7);
}

TEST_CASE("korean then english switches language per chunk" * doctest::skip(missing({modelPath(testModel), modelPath(vadModel), fixturePath("ko-long.wav"), fixturePath("en-long.wav")}))) {
    auto korean = readWav(fixturePath("ko-long.wav"));
    auto english = readWav(fixturePath("en-long.wav"));
    std::vector<float> samples = korean;
    samples.insert(samples.end(), kSampleRate, 0.0f);
    samples.insert(samples.end(), english.begin(), english.end());

    VoiceActivityDetector vad(modelPath(vadModel));
    SpeechMapBuilder builder(vad);
    builder.append(samples);
    auto chunks = ChunkPlanner{}.plan(builder.finish(samples.size() / static_cast<double>(kSampleRate)));
    REQUIRE(chunks.size() >= 2);

    WhisperEngine engine;
    engine.load(modelPath(testModel), std::nullopt, true);
    TranscriptionOptions options;
    options.languageMode = LanguageMode{LanguageMode::Kind::koreanEnglish, ""};
    options.beamSize = 1;
    ChunkTranscriber transcriber{options, {}, {}};
    CancelToken cancel;
    TranscriptionState state;
    std::vector<Segment> segments;
    auto reader = arrayReader(samples);
    while (state.nextChunk < static_cast<int>(chunks.size())) {
        auto result = transcriber.step(state, chunks, reader, engine, cancel);
        state = result.state;
        segments.insert(segments.end(), result.segments.begin(), result.segments.end());
    }
    const double koreanEnd = korean.size() / static_cast<double>(kSampleRate);
    std::string englishText;
    int first = 0;
    int second = 0;
    for (const auto& segment : segments) {
        if (segment.end <= koreanEnd + 0.5) {
            ++first;
            CHECK(segment.language == "ko");
        } else if (segment.start >= koreanEnd + 0.5) {
            ++second;
            CHECK(segment.language == "en");
            englishText += lowercase(segment.text);
        }
    }
    CHECK(first > 0);
    CHECK(second > 0);
    CHECK(englishText.find("format") != std::string::npos);
}

}
