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
};

// A file of the model catalog.
struct CatalogEntry {
  std::string role;  // "voice_detection", "transcription", "diarization", ...
  std::string file;  // name inside the models directory
  std::string url;
  std::uint64_t size = 0;
  std::string sha256;
  // A zip archive the app extracts into the models directory; the extracted
  // directory is `file` without ".zip".
  bool zip = false;
};

// Files the models of this build need.
std::vector<CatalogEntry> ModelCatalog();

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
