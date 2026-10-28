#ifndef SILLOK_BACKENDS_SORTFORMER_SORTFORMER_DIARIZER_HPP_
#define SILLOK_BACKENDS_SORTFORMER_SORTFORMER_DIARIZER_HPP_

#include <filesystem>
#include <memory>
#include <string>

#include "backends/ggml_devices.hpp"
#include "core/diarizer.hpp"

namespace sillok {

struct SortformerConfig {
  std::filesystem::path model;  // Sortformer GGUF file
  std::string name;             // for ModelInfo
  std::string preset;           // NeMo-Speech.cpp streaming geometry preset
  ComputeDevice device;
};

// Sortformer speaker diarization through NeMo-Speech.cpp. Throws ModelError
// if the model cannot be loaded.
std::unique_ptr<Diarizer> OpenSortformer(const SortformerConfig& config);

}  // namespace sillok

#endif  // SILLOK_BACKENDS_SORTFORMER_SORTFORMER_DIARIZER_HPP_
