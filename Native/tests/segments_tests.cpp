#include <doctest/doctest.h>

#include <set>

#include "segments.hpp"
#include "support.hpp"
#include "transcriber.hpp"

using namespace sillok;
using namespace sillok::testing;

static std::vector<std::string> texts(const std::vector<Segment>& segments) {
    std::vector<std::string> result;
    for (const auto& segment : segments) result.push_back(segment.text);
    return result;
}

TEST_CASE("words are assigned by midpoint around the cut") {
    double cut = 27;
    std::vector<Segment> previous{segmentOf({word(" one", 25.0, 25.8), word(" two", 26.5, 26.9), word(" three", 26.8, 27.4), word(" four", 27.1, 27.5)})};
    std::vector<Segment> next{segmentOf({word(" two", 26.6, 26.9), word(" three", 26.7, 27.3), word(" four", 27.1, 27.4), word(" five", 27.6, 28.0)})};
    auto kept = trimToChunk(previous, AudioChunk{0, 27.5, std::nullopt, cut});
    auto later = trimToChunk(next, AudioChunk{26.5, 40, cut, std::nullopt});
    kept.insert(kept.end(), later.begin(), later.end());
    CHECK(texts(kept) == std::vector<std::string>{"one two", "three four five"});
    CHECK(kept[0].end == doctest::Approx(26.9));
    CHECK(kept[1].start == doctest::Approx(26.7));
}

TEST_CASE("word with midpoint exactly at the cut goes to the later chunk") {
    std::vector<Segment> boundary{segmentOf({word(" a", 8, 9), word(" b", 9.8, 10.2)})};
    CHECK(texts(trimToChunk(boundary, AudioChunk{0, 10.5, std::nullopt, 10.0})) == std::vector<std::string>{"a"});
    CHECK(texts(trimToChunk(boundary, AudioChunk{9.5, 20, 10.0, std::nullopt})) == std::vector<std::string>{"b"});
}

TEST_CASE("segment without words uses the segment midpoint") {
    std::vector<Segment> segments{plainSegment("early", 9, 10.8), plainSegment("late", 9.6, 10.8)};
    CHECK(texts(trimToChunk(segments, AudioChunk{0, 10.5, std::nullopt, 10.0})) == std::vector<std::string>{"early"});
}

TEST_CASE("segment and word times are clamped to the chunk") {
    std::vector<Segment> overshooting{segmentOf({word(" a", 96, 97), word(" b", 97.5, 99.02)})};
    auto trimmed = trimToChunk(overshooting, AudioChunk{70.56, 98.57, std::nullopt, std::nullopt});
    CHECK(trimmed[0].end == doctest::Approx(98.57));
    CHECK(trimmed[0].words->back().end == doctest::Approx(98.57));
    CHECK(trimmed[0].text == "a b");
}

TEST_CASE("offset shifts segment and words") {
    auto shifted = offset(segmentOf({word(" x", 1, 2)}), 30);
    CHECK(shifted.start == 31);
    CHECK(shifted.end == 32);
    CHECK(shifted.words->front().start == 31);
}

TEST_CASE("short segment is not split") {
    auto original = segmentOf({word(" 짧은", 0, 1), word(" 문장.", 1, 2), word(" 끝", 2, 3)}, "ko");
    auto parts = SegmentSplitter{}.split(std::vector<Segment>{original});
    REQUIRE(parts.size() == 1);
    CHECK(parts[0].id == original.id);
}

TEST_CASE("long segment splits at sentence ends") {
    auto segment = segmentOf({word(" 첫", 0, 1), word(" 문장입니다.", 1, 2.5), word(" 둘째", 2.6, 4), word(" 문장도", 4, 5.5),
                              word(" 있습니다.", 5.5, 7), word(" 셋째", 7.1, 8), word(" 끝", 8, 9)}, "ko");
    segment.avgLogprob = -0.3f;
    auto parts = SegmentSplitter{}.split(std::vector<Segment>{segment});
    CHECK(texts(parts) == std::vector<std::string>{"첫 문장입니다.", "둘째 문장도 있습니다.", "셋째 끝"});
    REQUIRE(parts.size() == 3);
    CHECK(parts[0].start == 0);
    CHECK(parts[1].start == doctest::Approx(2.6));
    CHECK(parts[2].start == doctest::Approx(7.1));
    std::set<std::string> ids;
    for (const auto& part : parts) {
        CHECK(part.avgLogprob == -0.3f);
        CHECK(part.language == "ko");
        ids.insert(part.id);
    }
    CHECK(ids.size() == 3);
}

TEST_CASE("unpunctuated segment splits at the largest pause") {
    std::vector<WordTimestamp> words;
    for (int i = 0; i < 12; ++i) {
        double start = i + (i >= 5 ? 0.8 : 0);
        words.push_back(word(" 단어" + std::to_string(i), start, start + 0.9));
    }
    auto parts = SegmentSplitter{}.split(std::vector<Segment>{segmentOf(words, "ko")});
    REQUIRE(parts.size() >= 2);
    CHECK(parts[0].words->back().word == " 단어4");
    size_t total = 0;
    for (const auto& part : parts) {
        CHECK(part.end - part.start <= 7 + 1e-9);
        total += part.words->size();
    }
    CHECK(total == 12);
}

TEST_CASE("every split part respects the maximum and keeps all words") {
    for (uint64_t seed = 11; seed < 21; ++seed) {
        SeededGenerator generator{seed};
        double time = 0;
        std::vector<WordTimestamp> words;
        for (int i = 0; i < 80; ++i) {
            double length = generator.uniform(0.1, 1.2);
            std::string punctuation = generator.integer(0, 7) == 0 ? "." : "";
            words.push_back(word(" w" + std::to_string(i) + punctuation, time, time + length));
            time += length + generator.uniform(0, 0.6);
        }
        auto parts = SegmentSplitter{}.split(std::vector<Segment>{segmentOf(words)});
        std::vector<std::string> joined;
        for (const auto& part : parts) {
            CHECK((part.end - part.start <= 7 + 1e-9 || part.words->size() == 1));
            for (const auto& w : *part.words) joined.push_back(w.word);
        }
        std::vector<std::string> expected;
        for (const auto& w : words) expected.push_back(w.word);
        CHECK(joined == expected);
    }
}

TEST_CASE("silence hallucination is dropped") {
    Segment segment = plainSegment("x", 0, 1, -1.2f);
    segment.noSpeechProb = 0.7f;
    CHECK(SegmentFilter{}.apply({segment}).empty());
}

TEST_CASE("high no speech with confident text is kept") {
    Segment segment = plainSegment("x", 0, 1, -0.5f);
    segment.noSpeechProb = 0.7f;
    auto kept = SegmentFilter{}.apply({segment});
    REQUIRE(kept.size() == 1);
    CHECK(!kept[0].isLowConfidence);
}

TEST_CASE("weak segments are kept but marked low confidence") {
    Segment lowLogprob = plainSegment("x", 0, 1, -1.2f);
    lowLogprob.compressionRatio.reset();
    Segment repetitive = plainSegment("x", 0, 1, -0.3f);
    repetitive.compressionRatio = 2.5f;
    for (const auto& segment : {lowLogprob, repetitive}) {
        auto kept = SegmentFilter{}.apply({segment});
        REQUIRE(kept.size() == 1);
        CHECK(kept[0].isLowConfidence);
    }
}

TEST_CASE("filter thresholds are exclusive") {
    Segment segment = plainSegment("x", 0, 1, -1.0f);
    segment.noSpeechProb = 0.6f;
    segment.compressionRatio = 2.4f;
    auto kept = SegmentFilter{}.apply({segment});
    REQUIRE(kept.size() == 1);
    CHECK(!kept[0].isLowConfidence);
}

TEST_CASE("srt uses comma milliseconds and sequential indices") {
    Segment a = plainSegment("안녕하세요", 0.5, 2.25);
    Segment b = plainSegment("Hello", 3661.001, 3662.9996);
    CHECK(exportSubRip({a, b}) == "1\n00:00:00,500 --> 00:00:02,250\n안녕하세요\n\n2\n01:01:01,001 --> 01:01:03,000\nHello\n");
    CHECK(exportText({a, b}) == "안녕하세요\nHello\n");
}
