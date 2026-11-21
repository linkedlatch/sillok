#ifndef SILLOK_BACKENDS_QWEN_QWEN_ALIGNER_HPP_
#define SILLOK_BACKENDS_QWEN_QWEN_ALIGNER_HPP_

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "backends/ggml_devices.hpp"
#include "backends/qwen/llama_audio.hpp"
#include "backends/qwen/qwen_text.hpp"
#include "core/aligner.hpp"
#include "core/cancel.hpp"

namespace sillok::qwen {

// Qwen3-ForcedAligner (converted by llama.cpp's convert_hf_to_gguf.py)
// through llama.cpp, which loads its 5000-class output layer since b12425.
// Throws ModelError if it cannot be loaded.
class QwenAligner {
 public:
  QwenAligner(const std::filesystem::path& model, const std::filesystem::path& mmproj,
              const ComputeDevice& device, int threads);

  // Where the units of a transcript of `pcm` (16 kHz, at most 30 s) start
  // and end, in milliseconds, two per unit, as the model places them (before
  // FixTimestamps). `on_progress` receives the fraction done. Throws
  // ModelError when the audio and the units do not fit the model at once
  // (2048 positions: 30 s of audio leaves room for about 550 units).
  std::vector<int> Align(std::span<const float> pcm, std::span<const AlignUnit> units,
                         const std::function<void(double)>& on_progress, const StopToken& stop);

 private:
  LlamaAudio model_;
  llama_token slot_;  // <timestamp>
};

}  // namespace sillok::qwen

namespace sillok {

struct QwenAlignerConfig {
  std::filesystem::path model;   // Qwen3-ForcedAligner's language model (GGUF)
  std::filesystem::path mmproj;  // and its audio encoder
  std::string name;              // for ModelInfo
  ComputeDevice device;
  int threads = 4;
};

// Word times for Qwen3-ASR's text from Qwen3-ForcedAligner, as the qwen-asr
// package places them: requests of at most 30 s (the package takes 180 s in a
// larger context), shorter ones padded with
// silence to 0.5 s. Throws ModelError if the model cannot be loaded.
std::unique_ptr<Aligner> OpenQwenAligner(const QwenAlignerConfig& config);

}  // namespace sillok

#endif  // SILLOK_BACKENDS_QWEN_QWEN_ALIGNER_HPP_
