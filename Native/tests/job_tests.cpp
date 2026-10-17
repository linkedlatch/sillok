#include <doctest/doctest.h>

#include <chrono>
#include <fstream>
#include <set>

#include "job.hpp"
#include "support.hpp"
#include "whisper_engine.hpp"

using namespace sillok;
using namespace sillok::testing;

namespace {

struct EchoServices {
    int steps = 0;
    int diarizations = 0;

    JobServices services() {
        JobServices result;
        result.plan = [](const AudioReader&, double duration, const CancelToken&) {
            std::vector<AudioChunk> chunks;
            for (double start = 0; start < duration; start += 25) chunks.push_back(AudioChunk{start, std::min(start + 25, duration)});
            return chunks;
        };
        result.step = [this](const TranscriptionState& state, const std::vector<AudioChunk>& chunks, const AudioReader&, const CancelToken&) {
            ++steps;
            const auto& chunk = chunks.at(state.nextChunk);
            StepResult step{state, {}};
            step.state.nextChunk += 1;
            step.state.previousText = "p[" + state.previousText.value_or("") + "] n" + std::to_string(state.nextChunk);
            step.segments.push_back(plainSegment(*step.state.previousText, chunk.start, chunk.end));
            return step;
        };
        result.diarize = [this](const std::vector<Segment>& segments, const AudioReader&, double, const CancelToken&) {
            ++diarizations;
            auto labeled = segments;
            for (size_t i = 0; i < labeled.size(); ++i) labeled[i].speaker = std::to_string(i % 2 + 1);
            return labeled;
        };
        return result;
    }
};

std::filesystem::path temporaryCheckpoint() {
    return std::filesystem::temp_directory_path() / ("sillok-job-" + makeUUID()) / "checkpoint.json";
}

JobRequest request(const std::filesystem::path& checkpoint, bool diarize = false) {
    JobRequest value;
    value.modelId = "test";
    value.options.languageMode = LanguageMode{LanguageMode::Kind::fixed, "ko"};
    value.options.vocabulary = {"GOT"};
    value.options.speakerDiarization = diarize;
    value.checkpoint = checkpoint;
    value.duration = 100;
    return value;
}

struct Collected {
    std::vector<Segment> segments;
    std::vector<JobPhase> phases;
    std::vector<double> fractions;
};

ProgressHandler collector(Collected& collected, int stopAfter = 0, CancelToken* cancel = nullptr) {
    return [&collected, stopAfter, cancel, count = 0](const JobProgress& progress) mutable {
        if (progress.replacesSegments) collected.segments.clear();
        collected.segments.insert(collected.segments.end(), progress.newSegments.begin(), progress.newSegments.end());
        collected.phases.push_back(progress.phase);
        collected.fractions.push_back(progress.fractionCompleted);
        if (stopAfter > 0 && ++count >= stopAfter && cancel) cancel->cancel();
        return 0;
    };
}

std::vector<std::string> texts(const std::vector<Segment>& segments) {
    std::vector<std::string> result;
    for (const auto& segment : segments) result.push_back(segment.text);
    return result;
}

}

TEST_CASE("resumed job matches an uninterrupted job") {
    EchoServices reference;
    Collected full;
    const auto expected = runJob(request(temporaryCheckpoint()), silentReader(), reference.services(), collector(full), CancelToken{});
    CHECK(expected.size() == 4);
    CHECK(texts(full.segments) == texts(expected));

    const auto checkpoint = temporaryCheckpoint();
    EchoServices interrupted;
    Collected partial;
    CancelToken cancel;
    CHECK_THROWS_AS(runJob(request(checkpoint), silentReader(), interrupted.services(), collector(partial, 3, &cancel), cancel), Cancelled);
    CHECK(partial.segments.size() == 2);

    EchoServices resumed;
    Collected rest;
    const auto segments = runJob(request(checkpoint), silentReader(), resumed.services(), collector(rest), CancelToken{});
    CHECK(resumed.steps == 2);
    CHECK(texts(segments) == texts(expected));
    CHECK(texts(rest.segments) == texts(expected));
}

TEST_CASE("checkpoint with different options starts over") {
    const auto checkpoint = temporaryCheckpoint();
    EchoServices first;
    Collected partial;
    CancelToken cancel;
    CHECK_THROWS_AS(runJob(request(checkpoint), silentReader(), first.services(), collector(partial, 3, &cancel), cancel), Cancelled);

    auto changed = request(checkpoint);
    changed.options.vocabulary = {"other term"};
    EchoServices second;
    Collected collected;
    CHECK(runJob(changed, silentReader(), second.services(), collector(collected), CancelToken{}).size() == 4);
    CHECK(second.steps == 4);
}

TEST_CASE("progress starts analyzing and reaches completion in order") {
    EchoServices services;
    Collected collected;
    runJob(request(temporaryCheckpoint()), silentReader(), services.services(), collector(collected), CancelToken{});
    CHECK(collected.phases.front() == JobPhase::analyzing);
    CHECK(collected.fractions.front() == 0);
    CHECK(collected.fractions.back() == 1);
    CHECK(std::is_sorted(collected.fractions.begin(), collected.fractions.end()));
}

TEST_CASE("diarization replaces segments and is recorded in the checkpoint") {
    const auto checkpoint = temporaryCheckpoint();
    EchoServices services;
    Collected collected;
    const auto segments = runJob(request(checkpoint, true), silentReader(), services.services(), collector(collected), CancelToken{});
    CHECK(services.diarizations == 1);
    CHECK(std::find(collected.phases.begin(), collected.phases.end(), JobPhase::diarizing) != collected.phases.end());
    REQUIRE(collected.segments.size() == 4);
    CHECK(collected.segments[0].speaker == "1");
    CHECK(collected.segments[1].speaker == "2");
    CHECK(segments[3].speaker == "2");
    auto saved = Checkpoint::load(checkpoint);
    REQUIRE(saved);
    CHECK(saved->diarized);

    EchoServices again;
    Collected rerun;
    runJob(request(checkpoint, true), silentReader(), again.services(), collector(rerun), CancelToken{});
    CHECK(again.steps == 0);
    CHECK(again.diarizations == 0);
    CHECK(rerun.segments.size() == 4);
}

TEST_CASE("finished transcription resumes at diarization") {
    const auto checkpoint = temporaryCheckpoint();
    EchoServices first;
    Collected ignored;
    runJob(request(checkpoint), silentReader(), first.services(), collector(ignored), CancelToken{});
    auto saved = Checkpoint::load(checkpoint);
    REQUIRE(saved);
    saved->options.speakerDiarization = true;
    saved->save(checkpoint);

    EchoServices resumed;
    Collected collected;
    const auto segments = runJob(request(checkpoint, true), silentReader(), resumed.services(), collector(collected), CancelToken{});
    CHECK(resumed.steps == 0);
    CHECK(resumed.diarizations == 1);
    CHECK(segments.size() == 4);
    for (const auto& segment : collected.segments) CHECK(segment.speaker.has_value());
}

TEST_CASE("diarization is skipped when disabled") {
    EchoServices services;
    Collected collected;
    const auto segments = runJob(request(temporaryCheckpoint()), silentReader(), services.services(), collector(collected), CancelToken{});
    CHECK(services.diarizations == 0);
    for (const auto& segment : segments) CHECK_FALSE(segment.speaker.has_value());
}

TEST_CASE("progress handler can ask the job to wait between chunks") {
    EchoServices services;
    const auto started = std::chrono::steady_clock::now();
    runJob(request(temporaryCheckpoint()), silentReader(), services.services(),
           [](const JobProgress& progress) { return progress.phase == JobPhase::transcribing ? 100 : 0; }, CancelToken{});
    CHECK(std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(300));
}

TEST_CASE("checkpoints written by the Swift app still load") {
    const auto path = temporaryCheckpoint();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << R"({"schemaVersion":1,"modelID":"large-v3-turbo-q5_0","options":{"languageMode":{"auto":{}},"vocabulary":[],"beamSize":5,"maxPromptTokens":150},"chunks":[{"start":0,"end":25}],"state":{"nextChunk":1,"previousText":"안녕","segments":[{"id":"A","start":0,"end":2,"text":"안녕","language":"ko","isLowConfidence":false}]}})";
    auto checkpoint = Checkpoint::load(path);
    REQUIRE(checkpoint);
    CHECK(checkpoint->modelId == "large-v3-turbo-q5_0");
    CHECK(checkpoint->transcribed());
    CHECK(checkpoint->segments.size() == 1);
    CHECK_FALSE(checkpoint->diarized);
    CHECK_FALSE(checkpoint->options.speakerDiarization);
}

TEST_CASE("newer checkpoint versions are ignored") {
    const auto path = temporaryCheckpoint();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << R"({"schemaVersion":99,"modelId":"x","options":{},"chunks":[],"state":{"nextChunk":0}})";
    CHECK_FALSE(Checkpoint::load(path).has_value());
}

TEST_CASE("engine job transcribes a dialog and labels three speakers") {
    const auto diarizer = modelPath("Nemotron-3-Diarization.q8_0.gguf");
    std::vector<float> samples;
    std::vector<double> middles;
    for (int i = 1; i <= 7; ++i) {
        const auto path = fixturePath("dialog-" + std::to_string(i) + ".wav");
        if (!std::filesystem::exists(path) || !std::filesystem::exists(diarizer) || !std::filesystem::exists(modelPath(testModel))) {
            MESSAGE("skipped: run scripts/make-test-audio.sh and scripts/fetch-test-models.sh");
            return;
        }
        const auto part = readWav(path);
        middles.push_back(samples.size() / static_cast<double>(kSampleRate) + part.size() / 2.0 / kSampleRate);
        samples.insert(samples.end(), part.begin(), part.end());
        samples.insert(samples.end(), kSampleRate / 2, 0.0f);
    }

    WhisperEngine engine;
    engine.load(modelPath(testModel), std::nullopt, true);
    auto job = request(temporaryCheckpoint(), true);
    job.duration = samples.size() / static_cast<double>(kSampleRate);
    job.vadModel = modelPath(vadModel);
    job.diarizationModel = diarizer;
    const auto segments = runJob(job, arrayReader(samples), engineServices(engine, job), nullptr, CancelToken{});
    CHECK_FALSE(engine.isLoaded());

    std::vector<std::string> speakers;
    for (double time : middles) {
        auto distance = [time](const Segment& segment) { return std::max({segment.start - time, time - segment.end, 0.0}); };
        const auto nearest = std::min_element(segments.begin(), segments.end(),
                                              [&](const Segment& a, const Segment& b) { return distance(a) < distance(b); });
        REQUIRE(nearest != segments.end());
        REQUIRE(nearest->speaker);
        speakers.push_back(*nearest->speaker);
    }
    CHECK(std::set<std::string>(speakers.begin(), speakers.end()).size() == 3);
    CHECK(speakers[0] == speakers[2]);
    CHECK(speakers[0] == speakers[6]);
    CHECK(speakers[1] == speakers[4]);
    CHECK(speakers[3] == speakers[5]);
}
