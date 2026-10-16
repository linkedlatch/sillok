#include <doctest/doctest.h>

#include <cmath>

#include "chunking.hpp"
#include "support.hpp"

using namespace sillok;
using namespace sillok::testing;

namespace {

constexpr double frame = 0.1;

SpeechMap mapOf(double duration, std::vector<std::pair<double, double>> speech, std::vector<double> quiet = {}) {
    size_t count = static_cast<size_t>(std::lround(duration / frame));
    SpeechMap map{frame, std::vector<float>(count, 0.02f), std::vector<float>(count, 0.001f), duration};
    for (auto [start, end] : speech) {
        for (long i = std::lround(start / frame); i < std::lround(end / frame); ++i) {
            map.probabilities[i] = 0.9f;
            map.energies[i] = 0.2f;
        }
    }
    for (double time : quiet) map.energies[static_cast<size_t>(time / frame)] = 0.01f;
    return map;
}

bool approx(std::optional<double> a, std::optional<double> b) {
    if (!a || !b) return !a && !b;
    return std::abs(*a - *b) < 1e-6;
}

}

TEST_CASE("silence only produces no chunks") {
    CHECK(ChunkPlanner{}.plan(mapOf(60, {})).empty());
}

TEST_CASE("leading and trailing silence are excluded with padding") {
    auto chunks = ChunkPlanner{}.plan(mapOf(20, {{5, 8}}));
    REQUIRE(chunks.size() == 1);
    CHECK(approx(chunks[0].start, 4.8));
    CHECK(approx(chunks[0].end, 8.2));
    CHECK(!chunks[0].cutBefore);
    CHECK(!chunks[0].cutAfter);
}

TEST_CASE("regions within thirty seconds share one chunk") {
    auto chunks = ChunkPlanner{}.plan(mapOf(40, {{1, 10}, {12, 20}, {22, 28}}));
    REQUIRE(chunks.size() == 1);
    CHECK(approx(chunks[0].start, 0.8));
    CHECK(approx(chunks[0].end, 28.2));
}

TEST_CASE("half second silence is a split point when the chunk would exceed thirty seconds") {
    auto chunks = ChunkPlanner{}.plan(mapOf(40, {{0, 20}, {20.6, 35}}));
    REQUIRE(chunks.size() == 2);
    CHECK(approx(chunks[0].start, 0));
    CHECK(approx(chunks[0].end, 20.2));
    CHECK(approx(chunks[1].start, 20.4));
    CHECK(approx(chunks[1].end, 35.2));
    for (const auto& chunk : chunks) CHECK((!chunk.cutBefore && !chunk.cutAfter));
}

TEST_CASE("silence shorter than half a second is not a split point") {
    auto chunks = ChunkPlanner{}.plan(mapOf(40, {{0, 20}, {20.3, 35}}, {27.05}));
    REQUIRE(chunks.size() == 2);
    CHECK(approx(chunks[0].cutAfter, 27.05));
}

TEST_CASE("continuous speech is cut at the quietest point between 25 and 30 seconds with overlap") {
    auto chunks = ChunkPlanner{}.plan(mapOf(80, {{0, 70}}, {27.05, 53.55}));
    REQUIRE(chunks.size() == 3);
    CHECK(approx(chunks[0].start, 0));
    CHECK(approx(chunks[0].cutAfter, 27.05));
    CHECK(approx(chunks[0].end, 27.55));
    CHECK(approx(chunks[1].start, 26.55));
    CHECK(approx(chunks[1].cutBefore, 27.05));
    CHECK(approx(chunks[1].cutAfter, 53.55));
    CHECK(approx(chunks[1].end, 54.05));
    CHECK(approx(chunks[2].start, 53.05));
    CHECK(approx(chunks[2].cutBefore, 53.55));
    CHECK(!chunks[2].cutAfter);
    CHECK(approx(chunks[2].end, 70.2));
}

TEST_CASE("forced cut stays inside the search window without quiet frames") {
    auto chunks = ChunkPlanner{}.plan(mapOf(100, {{0, 95}}));
    for (size_t i = 0; i + 1 < chunks.size(); ++i) {
        REQUIRE(chunks[i].cutAfter);
        double cut = *chunks[i].cutAfter;
        CHECK(cut >= chunks[i].start + 25);
        CHECK(cut <= chunks[i].start + 29.5);
        CHECK(approx(chunks[i + 1].cutBefore, cut));
        CHECK(approx(chunks[i + 1].start, cut - 0.5));
        CHECK(approx(chunks[i].end, cut + 0.5));
    }
}

TEST_CASE("every chunk is at most thirty seconds and covers all speech") {
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        SeededGenerator generator{seed};
        SpeechMap map{frame, {}, {}, 0};
        while (map.probabilities.size() < 6000) {
            bool speech = generator.integer(0, 1) == 1;
            int length = speech ? generator.integer(1, 500) : generator.integer(1, 30);
            for (int i = 0; i < length; ++i) {
                map.probabilities.push_back(speech ? 0.9f : 0.05f);
                map.energies.push_back(static_cast<float>(generator.uniform(0, 1)));
            }
        }
        map.duration = static_cast<double>(map.probabilities.size()) * frame;
        auto chunks = ChunkPlanner{}.plan(map);
        for (const auto& chunk : chunks) CHECK(chunk.duration() <= 30 + 1e-9);
        for (size_t i = 0; i < map.probabilities.size(); ++i) {
            if (map.probabilities[i] < 0.5f) continue;
            double t = (static_cast<double>(i) + 0.5) * frame;
            bool covered = false;
            for (const auto& chunk : chunks) covered = covered || (t >= chunk.start && t <= chunk.end);
            CHECK(covered);
        }
    }
}
