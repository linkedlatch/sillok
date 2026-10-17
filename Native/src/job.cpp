#include "job.hpp"

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>

#include "chunking.hpp"
#include "diarization.hpp"
#include "whisper_engine.hpp"

namespace sillok {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

double fraction(double time, double duration) {
    return duration > 0 ? std::min(time / duration, 1.0) : 1.0;
}

void pause(int milliseconds, const CancelToken& cancel) {
    for (int waited = 0; waited < milliseconds; waited += 100) {
        if (cancel.isCancelled()) throw Cancelled();
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(100, milliseconds - waited)));
    }
}

const char* phaseName(JobPhase phase) {
    switch (phase) {
    case JobPhase::analyzing: return "analyzing";
    case JobPhase::transcribing: return "transcribing";
    case JobPhase::diarizing: return "diarizing";
    }
    return "transcribing";
}

}

std::optional<Checkpoint> Checkpoint::load(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    try {
        const json document = json::parse(input);
        if (document.value("schemaVersion", 0) > currentSchemaVersion) return std::nullopt;
        return document.get<Checkpoint>();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void Checkpoint::save(const fs::path& path) const {
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << json(*this).dump();
        if (!output) throw std::runtime_error("failed to save checkpoint");
    }
    fs::rename(temporary, path);
}

std::vector<Segment> runJob(const JobRequest& request, const AudioReader& read, const JobServices& services,
                            const ProgressHandler& progress, const CancelToken& cancel) {
    auto report = [&](JobProgress update) { return progress ? progress(update) : 0; };

    auto checkpoint = Checkpoint::load(request.checkpoint);
    if (checkpoint && (checkpoint->modelId != request.modelId || json(checkpoint->options) != json(request.options))) {
        checkpoint.reset();
    }
    if (checkpoint) {
        report({JobPhase::transcribing, fraction(checkpoint->resumeFrom(), request.duration), checkpoint->resumeFrom(),
                checkpoint->segments, true});
    } else {
        report({JobPhase::analyzing, 0, 0, {}, false});
        Checkpoint fresh{request.modelId, request.options, services.plan(read, request.duration, cancel), {}, {}, false};
        fresh.save(request.checkpoint);
        checkpoint = std::move(fresh);
    }

    while (!checkpoint->transcribed()) {
        if (cancel.isCancelled()) throw Cancelled();
        StepResult result = services.step(checkpoint->state, checkpoint->chunks, read, cancel);
        checkpoint->state = result.state;
        checkpoint->segments.insert(checkpoint->segments.end(), result.segments.begin(), result.segments.end());
        checkpoint->save(request.checkpoint);
        const int wait = report({JobPhase::transcribing, fraction(checkpoint->resumeFrom(), request.duration),
                                 checkpoint->resumeFrom(), result.segments, false});
        if (wait > 0 && !checkpoint->transcribed()) pause(wait, cancel);
    }

    if (request.options.speakerDiarization && !checkpoint->diarized) {
        if (cancel.isCancelled()) throw Cancelled();
        report({JobPhase::diarizing, 1, request.duration, {}, false});
        checkpoint->segments = services.diarize(checkpoint->segments, read, request.duration, cancel);
        checkpoint->diarized = true;
        checkpoint->save(request.checkpoint);
        report({JobPhase::diarizing, 1, request.duration, checkpoint->segments, true});
    }

    report({request.options.speakerDiarization ? JobPhase::diarizing : JobPhase::transcribing, 1, request.duration, {}, false});
    return checkpoint->segments;
}

JobServices engineServices(WhisperEngine& engine, const JobRequest& request) {
    JobServices services;
    services.plan = [request](const AudioReader& read, double duration, const CancelToken& cancel) {
        VoiceActivityDetector vad(request.vadModel);
        SpeechMapBuilder builder(vad);
        const double window = VoiceActivityDetector::frameSize * 1875.0 / kSampleRate;
        for (double start = 0; start < duration; start += window) {
            if (cancel.isCancelled()) throw Cancelled();
            const auto samples = read(start, start + window);
            if (samples.empty()) break;
            builder.append(samples);
        }
        return ChunkPlanner{}.plan(builder.finish(duration));
    };
    services.step = [&engine, options = request.options](const TranscriptionState& state, const std::vector<AudioChunk>& chunks,
                                                         const AudioReader& read, const CancelToken& cancel) {
        return ChunkTranscriber{options, {}, {}}.step(state, chunks, read, engine, cancel);
    };
    services.diarize = [&engine, request](const std::vector<Segment>& segments, const AudioReader& read, double duration,
                                          const CancelToken& cancel) {
        if (!request.diarizationModel || segments.empty()) return segments;
        engine.unload();
        SpeakerDiarizer diarizer(*request.diarizationModel, request.useGPU);
        return SpeakerAssigner{}.assign(segments, diarizer.diarize(read, duration, cancel));
    };
    return services;
}

void to_json(json& j, const Checkpoint& value) {
    json state = value.state;
    state["segments"] = value.segments;
    state["diarized"] = value.diarized;
    j = json{{"schemaVersion", Checkpoint::currentSchemaVersion}, {"modelId", value.modelId}, {"options", value.options},
             {"chunks", value.chunks}, {"state", state}};
}

void from_json(const json& j, Checkpoint& value) {
    value.modelId = j.contains("modelId") ? j.at("modelId").get<std::string>() : j.value("modelID", std::string());
    j.at("options").get_to(value.options);
    j.at("chunks").get_to(value.chunks);
    const json& state = j.at("state");
    state.get_to(value.state);
    value.segments = state.value("segments", std::vector<Segment>{});
    value.diarized = state.value("diarized", false);
}

void to_json(json& j, const JobProgress& value) {
    j = json{{"phase", phaseName(value.phase)}, {"fractionCompleted", value.fractionCompleted},
             {"lastProcessedTimestamp", value.lastProcessedTimestamp}, {"newSegments", value.newSegments},
             {"replacesSegments", value.replacesSegments}};
}

void from_json(const json& j, JobRequest& value) {
    j.at("modelId").get_to(value.modelId);
    value.options = j.value("options", TranscriptionOptions{});
    value.checkpoint = utf8Path(j.at("checkpoint").get<std::string>());
    j.at("duration").get_to(value.duration);
    value.vadModel = utf8Path(j.value("vadModel", std::string()));
    if (auto it = j.find("diarizationModel"); it != j.end() && !it->is_null()) value.diarizationModel = utf8Path(it->get<std::string>());
    value.useGPU = j.value("useGPU", true);
}

}
