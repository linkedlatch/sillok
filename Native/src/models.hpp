#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "types.hpp"

namespace sillok {

enum class ModelKind { whisper, coreMLEncoder, vad, diarization };

struct ModelEntry {
    std::string id;
    ModelKind kind = ModelKind::whisper;
    std::string displayName;
    std::string fileName;
    int64_t size = 0;
    std::string sha256;
    std::vector<std::string> urls;
    std::optional<std::string> coreMLEncoderId;
    std::string license;

    std::string installedName() const;
};

struct ModelManifest {
    int version = 1;
    std::vector<ModelEntry> models;

    const ModelEntry* entry(const std::string& id) const;
    const ModelEntry* coreMLEncoder(const ModelEntry& whisper) const;
};

struct ModelSelection {
    static constexpr const char* defaultWhisperId = "large-v3-turbo-q5_0";
    static constexpr const char* vadId = "silero-v6.2.0";
    static constexpr const char* diarizationId = "nemotron-3-diarization-q8_0";
    static constexpr int64_t runtimeOverhead = int64_t{1} << 30;

    static std::vector<ModelEntry> required(const ModelManifest& manifest, const std::string& whisperId, bool useCoreML, bool diarization);
    static bool shouldUseCoreML(const ModelEntry& whisper, const ModelEntry* encoder, int64_t availableMemory, bool countEncoder);
};

struct ChecksumMismatch : std::runtime_error {
    explicit ChecksumMismatch(const std::string& fileName);
};

std::string sha256Hex(const std::filesystem::path& file);
bool isInstalled(const ModelEntry& entry, const std::filesystem::path& root);
void installVerified(const std::filesystem::path& file, const ModelEntry& entry, const std::filesystem::path& root);

void to_json(nlohmann::json& j, const ModelEntry& value);
void from_json(const nlohmann::json& j, ModelEntry& value);
void to_json(nlohmann::json& j, const ModelManifest& value);
void from_json(const nlohmann::json& j, ModelManifest& value);

}
