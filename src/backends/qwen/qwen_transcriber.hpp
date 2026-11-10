#ifndef SILLOK_BACKENDS_QWEN_QWEN_TRANSCRIBER_HPP_
#define SILLOK_BACKENDS_QWEN_QWEN_TRANSCRIBER_HPP_

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "backends/ggml_devices.hpp"
#include "core/transcriber.hpp"

namespace sillok {

struct QwenConfig {
  std::filesystem::path asr_model;       // Qwen3-ASR's language model (GGUF)
  std::filesystem::path asr_mmproj;      // and its audio encoder
  std::filesystem::path aligner_model;   // Qwen3-ForcedAligner's language model
  std::filesystem::path aligner_mmproj;  // and its audio encoder
  std::string name;                      // for ModelInfo
  ComputeDevice device;
  int threads = 4;
  // The languages it may be told or tell from the audio (codes).
  std::vector<std::string> languages;
};

// Qwen3-ASR through llama.cpp, with word times from Qwen3-ForcedAligner, as
// the qwen-asr package runs them. Both models stay loaded. A request is at
// most 180 s, the aligner's limit. Throws ModelError if a model cannot be
// loaded.
std::unique_ptr<Transcriber> OpenQwen(const QwenConfig& config);

}  // namespace sillok

#endif  // SILLOK_BACKENDS_QWEN_QWEN_TRANSCRIBER_HPP_
