#ifndef SILLOK_BACKENDS_MODELS_HPP_
#define SILLOK_BACKENDS_MODELS_HPP_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/model_provider.hpp"

// The one place that knows which concrete models sillok uses. Implemented in
// models.cpp; the C ABI layer sees only these declarations (tests link fakes).

namespace sillok {

enum class Device { kCpu, kGpu };

struct EngineConfig {
  std::filesystem::path models_dir;
  // kGpu falls back to the CPU when the GPU cannot be used; ModelInfo then
  // reports it.
  Device device = Device::kCpu;
  int gpu_index = 0;  // among the GPUs ListDevices() reports
  int threads = 0;    // CPU threads; 0 picks a default
  // Catalog id of the transcription model; empty for the recommended one.
  std::string transcription_model;
};

// A file of a catalog model.
struct CatalogFile {
  std::string file;  // name inside the models directory
  std::string url;
  std::uint64_t size = 0;
  std::string sha256;
  // A zip archive the app extracts into the models directory; the extracted
  // directory is `file` without ".zip". Archives are optional accelerators.
  bool zip = false;
};

// A model the app can download: its files and how to describe it.
struct CatalogModel {
  std::string id;            // e.g. "whisper-large-v3-turbo-q5_0"
  std::string role;          // "voice_detection", "transcription", "diarization"
  std::string family;        // e.g. "large-v3-turbo", for apps to describe
  std::string quantization;  // e.g. "q5_0", "q8_0", "f16"
  bool recommended = false;  // its role's default
  std::vector<CatalogFile> files;
};

// The models of this build; one model of each role is recommended.
std::vector<CatalogModel> ModelCatalog();

struct DeviceInfo {
  std::string name;
  std::string description;
  std::string type;  // "cpu", "gpu", "igpu" or "accel"
  std::uint64_t memory_total = 0;
  std::uint64_t memory_free = 0;
};

// Compute devices this build can use.
std::vector<DeviceInfo> ListDevices();

// Models loaded from config.models_dir on demand.
std::unique_ptr<ModelProvider> OpenModels(const EngineConfig& config);

}  // namespace sillok

#endif  // SILLOK_BACKENDS_MODELS_HPP_
