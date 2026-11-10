#ifndef SILLOK_BACKENDS_QWEN_QWEN_ALIGNER_HPP_
#define SILLOK_BACKENDS_QWEN_QWEN_ALIGNER_HPP_

#include <filesystem>
#include <functional>
#include <span>
#include <vector>

#include "backends/ggml_devices.hpp"
#include "backends/qwen/llama_audio.hpp"
#include "backends/qwen/qwen_text.hpp"
#include "core/cancel.hpp"

namespace sillok::qwen {

// Qwen3-ForcedAligner (converted by scripts/models/convert_qwen_aligner.py)
// through llama.cpp. Throws ModelError if it cannot be loaded.
class QwenAligner {
 public:
  QwenAligner(const std::filesystem::path& model, const std::filesystem::path& mmproj,
              const ComputeDevice& device, int threads);

  // Where the units of a transcript of `pcm` (16 kHz, at most 180 s) start
  // and end, in milliseconds, two per unit, as the model places them (before
  // FixTimestamps). `on_progress` receives the fraction done.
  std::vector<int> Align(std::span<const float> pcm, std::span<const AlignUnit> units,
                         const std::function<void(double)>& on_progress, const StopToken& stop);

 private:
  LlamaAudio model_;
  llama_token slot_;  // <timestamp>
  int classes_;
  int segment_ms_;
};

}  // namespace sillok::qwen

#endif  // SILLOK_BACKENDS_QWEN_QWEN_ALIGNER_HPP_
