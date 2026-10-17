#include "sillok.h"

#include <cstring>
#include <memory>
#include <stdexcept>

#include "chunking.hpp"
#include "diarization.hpp"
#include "transcriber.hpp"
#include "whisper_engine.hpp"

using nlohmann::json;
using namespace sillok;

struct sillok_engine {
    WhisperEngine engine;
};

struct sillok_vad {
    std::unique_ptr<VoiceActivityDetector> detector;
};

struct sillok_speech_map {
    SpeechMapBuilder builder;
};

struct sillok_cancel {
    CancelToken token;
};

struct sillok_buffer {
    std::vector<float> samples;
};

struct sillok_diarizer {
    std::unique_ptr<SpeakerDiarizer> diarizer;
};

static char* copy(const std::string& text) {
    char* result = static_cast<char*>(std::malloc(text.size() + 1));
    std::memcpy(result, text.c_str(), text.size() + 1);
    return result;
}

static char* success(const json& value) {
    return copy(json{{"ok", true}, {"value", value}}.dump());
}

static char* failure(const std::string& message, bool cancelled = false) {
    return copy(json{{"ok", false}, {"error", message}, {"cancelled", cancelled}}.dump());
}

template <typename Body>
static char* guarded(Body body) {
    try {
        return success(body());
    } catch (const Cancelled&) {
        return failure("cancelled", true);
    } catch (const std::exception& error) {
        return failure(error.what());
    }
}

static std::vector<float> samplesOf(const float* samples, size_t count) {
    return samples ? std::vector<float>(samples, samples + count) : std::vector<float>{};
}

extern "C" {

void sillok_free(char* string) {
    std::free(string);
}

void sillok_buffer_assign(sillok_buffer* buffer, const float* samples, size_t count) {
    buffer->samples = samplesOf(samples, count);
}

char* sillok_system_info(void) {
    return copy(systemInfo());
}

int sillok_vad_frame_size(void) {
    return VoiceActivityDetector::frameSize;
}

sillok_cancel* sillok_cancel_new(void) {
    return new sillok_cancel();
}

void sillok_cancel_set(sillok_cancel* cancel) {
    cancel->token.cancel();
}

void sillok_cancel_free(sillok_cancel* cancel) {
    delete cancel;
}

sillok_engine* sillok_engine_new(void) {
    return new sillok_engine();
}

void sillok_engine_free(sillok_engine* engine) {
    delete engine;
}

char* sillok_engine_load(sillok_engine* engine, const char* ggml_path, const char* coreml_encoder_path, int use_gpu) {
    return guarded([&] {
        std::optional<std::filesystem::path> encoder;
        if (coreml_encoder_path) encoder = std::filesystem::path(coreml_encoder_path);
        engine->engine.load(ggml_path, encoder, use_gpu != 0);
        return json(true);
    });
}

void sillok_engine_unload(sillok_engine* engine) {
    engine->engine.unload();
}

char* sillok_engine_detect_language(sillok_engine* engine, const float* samples, size_t count, const char* allowed_json) {
    return guarded([&] {
        std::optional<std::set<std::string>> allowed;
        if (allowed_json) allowed = json::parse(allowed_json).get<std::set<std::string>>();
        return json(engine->engine.detectLanguage(samplesOf(samples, count), allowed));
    });
}

char* sillok_engine_transcribe(sillok_engine* engine, const float* samples, size_t count, const char* options_json, sillok_cancel* cancel) {
    return guarded([&] {
        CancelToken none;
        auto options = json::parse(options_json).get<ChunkTranscribeOptions>();
        return json(engine->engine.transcribe(samplesOf(samples, count), options, cancel ? cancel->token : none));
    });
}

sillok_vad* sillok_vad_new(const char* model_path, char** error) {
    try {
        return new sillok_vad{std::make_unique<VoiceActivityDetector>(model_path)};
    } catch (const std::exception& failure) {
        if (error) *error = copy(failure.what());
        return nullptr;
    }
}

void sillok_vad_free(sillok_vad* vad) {
    delete vad;
}

sillok_speech_map* sillok_speech_map_new(sillok_vad* vad) {
    return new sillok_speech_map{SpeechMapBuilder(*vad->detector)};
}

void sillok_speech_map_free(sillok_speech_map* map) {
    delete map;
}

char* sillok_speech_map_append(sillok_speech_map* map, const float* samples, size_t count) {
    return guarded([&] {
        map->builder.append(samplesOf(samples, count));
        return json(true);
    });
}

char* sillok_speech_map_plan(sillok_speech_map* map, double duration, const char* planner_json) {
    return guarded([&] {
        ChunkPlanner planner = planner_json ? json::parse(planner_json).get<ChunkPlanner>() : ChunkPlanner{};
        return json(planner.plan(map->builder.finish(duration)));
    });
}

char* sillok_transcribe_step(sillok_engine* engine, const char* state_json, const char* chunks_json, const char* options_json,
                             sillok_read_audio read, void* read_context, sillok_cancel* cancel) {
    return guarded([&] {
        CancelToken none;
        auto state = json::parse(state_json).get<TranscriptionState>();
        auto chunks = json::parse(chunks_json).get<std::vector<AudioChunk>>();
        ChunkTranscriber transcriber{json::parse(options_json).get<TranscriptionOptions>(), {}, {}};
        AudioReader reader = [&](double start, double end) {
            sillok_buffer buffer;
            read(read_context, start, end, &buffer);
            return buffer.samples;
        };
        StepResult result = transcriber.step(state, chunks, reader, engine->engine, cancel ? cancel->token : none);
        return json{{"state", result.state}, {"segments", result.segments}};
    });
}

sillok_diarizer* sillok_diarizer_new(const char* model_path, int use_gpu, char** error) {
    try {
        return new sillok_diarizer{std::make_unique<SpeakerDiarizer>(model_path, use_gpu != 0)};
    } catch (const std::exception& failure) {
        if (error) *error = copy(failure.what());
        return nullptr;
    }
}

void sillok_diarizer_free(sillok_diarizer* diarizer) {
    delete diarizer;
}

char* sillok_diarize(sillok_diarizer* diarizer, const char* segments_json, double duration,
                     sillok_read_audio read, void* read_context, sillok_cancel* cancel) {
    return guarded([&] {
        CancelToken none;
        auto segments = json::parse(segments_json).get<std::vector<Segment>>();
        AudioReader reader = [&](double start, double end) {
            sillok_buffer buffer;
            read(read_context, start, end, &buffer);
            return buffer.samples;
        };
        auto turns = diarizer->diarizer->diarize(reader, duration, cancel ? cancel->token : none);
        return json(SpeakerAssigner{}.assign(segments, turns));
    });
}

char* sillok_export(const char* segments_json, const char* format) {
    return guarded([&] {
        auto segments = json::parse(segments_json).get<std::vector<Segment>>();
        if (std::strcmp(format, "srt") == 0) return json(exportSubRip(segments));
        if (std::strcmp(format, "txt") == 0) return json(exportText(segments));
        throw std::runtime_error(std::string("unknown format ") + format);
    });
}

}
