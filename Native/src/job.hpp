#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "transcriber.hpp"
#include "types.hpp"

namespace sillok {

struct Checkpoint {
    static constexpr int currentSchemaVersion = 1;

    std::string modelId;
    TranscriptionOptions options;
    std::vector<AudioChunk> chunks;
    TranscriptionState state;
    std::vector<Segment> segments;
    bool diarized = false;

    double resumeFrom() const { return state.nextChunk == 0 ? 0 : chunks.at(state.nextChunk - 1).end; }
    bool transcribed() const { return state.nextChunk >= static_cast<int>(chunks.size()); }

    static std::optional<Checkpoint> load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
};

enum class JobPhase { analyzing, transcribing, diarizing };

struct JobProgress {
    JobPhase phase = JobPhase::analyzing;
    double fractionCompleted = 0;
    double lastProcessedTimestamp = 0;
    std::vector<Segment> newSegments;
    bool replacesSegments = false;
};

struct JobRequest {
    std::string modelId;
    TranscriptionOptions options;
    std::filesystem::path checkpoint;
    double duration = 0;
    std::filesystem::path vadModel;
    std::optional<std::filesystem::path> diarizationModel;
    bool useGPU = true;
};

struct JobServices {
    std::function<std::vector<AudioChunk>(const AudioReader&, double duration, const CancelToken&)> plan;
    std::function<StepResult(const TranscriptionState&, const std::vector<AudioChunk>&, const AudioReader&, const CancelToken&)> step;
    std::function<std::vector<Segment>(const std::vector<Segment>&, const AudioReader&, double duration, const CancelToken&)> diarize;
};

using ProgressHandler = std::function<int(const JobProgress&)>;

class WhisperEngine;

JobServices engineServices(WhisperEngine& engine, const JobRequest& request);

std::vector<Segment> runJob(const JobRequest& request, const AudioReader& read, const JobServices& services,
                            const ProgressHandler& progress, const CancelToken& cancel);

void to_json(nlohmann::json& j, const Checkpoint& value);
void from_json(const nlohmann::json& j, Checkpoint& value);
void to_json(nlohmann::json& j, const JobProgress& value);
void from_json(const nlohmann::json& j, JobRequest& value);

}
