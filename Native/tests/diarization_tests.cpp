#include <doctest/doctest.h>

#include "diarization.hpp"
#include "support.hpp"
#include "transcriber.hpp"

using namespace sillok;
using namespace sillok::testing;

static const std::string diarizationModel = "Nemotron-3-Diarization.q8_0.gguf";

TEST_CASE("segment without words takes the speaker with the most overlap") {
    auto segment = plainSegment("hello there", 1.0, 5.0);
    auto result = SpeakerAssigner{}.assign({segment}, {{0.0, 2.0, 1}, {2.0, 6.0, 2}});
    REQUIRE(result.size() == 1);
    CHECK(result[0].speaker == "2");
    CHECK(result[0].id == segment.id);
}

TEST_CASE("segment is split where the speaker changes") {
    auto segment = segmentOf({word(" one", 0.0, 0.6), word(" two", 0.6, 1.2), word(" three", 1.4, 2.0), word(" four", 2.0, 2.6),
                              word(" five", 2.6, 3.2)});
    segment.end = 3.5;
    auto result = SpeakerAssigner{}.assign({segment}, {{0.0, 1.3, 1}, {1.3, 4.0, 2}});
    REQUIRE(result.size() == 2);
    CHECK(result[0].id == segment.id);
    CHECK(result[0].text == "one two");
    CHECK(result[0].speaker == "1");
    CHECK(result[0].start == 0.0);
    CHECK(result[0].end == 1.2);
    CHECK(result[1].id != segment.id);
    CHECK(result[1].text == "three four five");
    CHECK(result[1].speaker == "2");
    CHECK(result[1].start == 1.4);
    CHECK(result[1].end == 3.5);
}

TEST_CASE("short speaker runs are absorbed into neighbours") {
    auto segment = segmentOf({word(" a", 0.0, 0.8), word(" b", 0.8, 1.6), word(" c", 1.6, 1.9), word(" d", 1.9, 2.7),
                              word(" e", 2.7, 3.5)});
    auto result = SpeakerAssigner{}.assign({segment}, {{0.0, 1.6, 1}, {1.6, 1.9, 2}, {1.9, 4.0, 1}});
    REQUIRE(result.size() == 1);
    CHECK(result[0].speaker == "1");
    CHECK(result[0].text == segment.text);
}

TEST_CASE("leading short run joins the following speaker") {
    auto segment = segmentOf({word(" so", 0.0, 0.3), word(" we", 0.3, 1.2), word(" start", 1.2, 2.4)});
    auto result = SpeakerAssigner{}.assign({segment}, {{0.0, 0.3, 1}, {0.3, 3.0, 2}});
    REQUIRE(result.size() == 1);
    CHECK(result[0].speaker == "2");
}

TEST_CASE("words outside every turn take the nearest speaker") {
    auto segment = plainSegment("late words", 10.0, 11.0);
    auto result = SpeakerAssigner{}.assign({segment}, {{0.0, 2.0, 1}, {8.0, 9.5, 3}});
    REQUIRE(result.size() == 1);
    CHECK(result[0].speaker == "3");
}

TEST_CASE("segments stay unlabeled without speaker turns") {
    auto segment = plainSegment("nothing", 0.0, 1.0);
    auto result = SpeakerAssigner{}.assign({segment}, {});
    REQUIRE(result.size() == 1);
    CHECK_FALSE(result[0].speaker.has_value());
}

TEST_CASE("exports label speakers only when there are several") {
    auto first = plainSegment("안녕하세요", 0.0, 1.0);
    auto second = plainSegment("네 반갑습니다", 1.0, 2.0);
    first.speaker = "1";
    second.speaker = "1";
    CHECK(exportText({first, second}) == "안녕하세요\n네 반갑습니다\n");
    second.speaker = "2";
    CHECK(exportText({first, second}) == "Speaker 1: 안녕하세요\nSpeaker 2: 네 반갑습니다\n");
    CHECK(exportSubRip({first, second}) ==
          "1\n00:00:00,000 --> 00:00:01,000\n[Speaker 1] 안녕하세요\n\n2\n00:00:01,000 --> 00:00:02,000\n[Speaker 2] 네 반갑습니다\n");
    CHECK(exportText({first, second}, "화자 {speaker}") == "화자 1: 안녕하세요\n화자 2: 네 반갑습니다\n");
}

namespace {

struct Dialog {
    std::vector<float> samples;
    std::vector<SpeakerTurn> truth;
};

std::optional<Dialog> loadDialog() {
    const int speakers[] = {1, 2, 1, 3, 2, 3, 1};
    Dialog dialog;
    for (int i = 0; i < 7; ++i) {
        auto path = fixturePath("dialog-" + std::to_string(i + 1) + ".wav");
        if (!std::filesystem::exists(path)) return std::nullopt;
        auto part = readWav(path);
        const double start = dialog.samples.size() / static_cast<double>(kSampleRate);
        dialog.samples.insert(dialog.samples.end(), part.begin(), part.end());
        dialog.truth.push_back({start, start + part.size() / static_cast<double>(kSampleRate), speakers[i]});
        dialog.samples.insert(dialog.samples.end(), kSampleRate / 2, 0.0f);
    }
    return dialog;
}

std::vector<SpeakerTurn> mergedTurns(const std::vector<SpeakerTurn>& turns) {
    std::vector<SpeakerTurn> merged;
    for (const auto& turn : turns) {
        if (!merged.empty() && merged.back().speaker == turn.speaker && turn.start - merged.back().end < 1.0) {
            merged.back().end = turn.end;
        } else {
            merged.push_back(turn);
        }
    }
    return merged;
}

}

TEST_CASE("diarizer separates three speakers in a dialog") {
    auto dialog = loadDialog();
    if (!dialog || !std::filesystem::exists(modelPath(diarizationModel))) {
        MESSAGE("skipped: run scripts/make-test-audio.sh and scripts/fetch-test-models.sh");
        return;
    }
    SpeakerDiarizer diarizer(modelPath(diarizationModel), true);
    CancelToken cancel;
    const double duration = dialog->samples.size() / static_cast<double>(kSampleRate);
    auto turns = mergedTurns(diarizer.diarize(arrayReader(dialog->samples), duration, cancel));
    REQUIRE(turns.size() == dialog->truth.size());
    for (size_t i = 0; i < turns.size(); ++i) {
        CHECK(turns[i].speaker == dialog->truth[i].speaker);
        CHECK(std::abs(turns[i].start - dialog->truth[i].start) < 1.0);
        CHECK(std::abs(turns[i].end - dialog->truth[i].end) < 1.0);
    }
}

TEST_CASE("diarizer stops when cancelled") {
    auto dialog = loadDialog();
    if (!dialog || !std::filesystem::exists(modelPath(diarizationModel))) return;
    SpeakerDiarizer diarizer(modelPath(diarizationModel), true);
    CancelToken cancel;
    cancel.cancel();
    CHECK_THROWS_AS(diarizer.diarize(arrayReader(dialog->samples), dialog->samples.size() / static_cast<double>(kSampleRate), cancel), Cancelled);
}
