#include "backends/qwen/qwen_aligner.hpp"

#include <algorithm>
#include <string>

#include <mtmd.h>

#include "core/model_provider.hpp"

namespace sillok::qwen {

QwenAligner::QwenAligner(const std::filesystem::path& model, const std::filesystem::path& mmproj,
                         const ComputeDevice& device, int threads)
    : model_({.text_model = model,
              .mmproj = mmproj,
              .device = device,
              .threads = threads,
              .context = 16384,
              .embeddings = false}),
      slot_(model_.Token("<timestamp>")),
      classes_(model_.MetaNumber("qwen3_forced_aligner.classes")),
      segment_ms_(model_.MetaNumber("qwen3_forced_aligner.segment_ms")) {
  if (classes_ <= 0 || classes_ > model_.row_size() || segment_ms_ <= 0) {
    throw ModelError("not a converted Qwen3-ForcedAligner");
  }
}

std::vector<int> QwenAligner::Align(std::span<const float> pcm, std::span<const AlignUnit> units,
                                    const std::function<void(double)>& on_progress,
                                    const StopToken& stop) {
  // The package's input: the audio, then each unit followed by two slots.
  std::string prompt = mtmd_default_marker();
  for (const AlignUnit& unit : units) prompt += unit.text + "<timestamp><timestamp>";
  std::vector<int> times;
  times.reserve(units.size() * 2);
  // The output layer's first rows are the classes of 80 ms; the rest are 0.
  model_.Feed(
      prompt, pcm, AudioTokens(pcm.size()), [&](llama_token token) { return token == slot_; },
      [&](llama_token, const float* logits) {
        times.push_back(static_cast<int>(std::max_element(logits, logits + classes_) - logits) *
                        segment_ms_);
      },
      false, on_progress, stop);
  if (times.size() != units.size() * 2) {
    throw ModelError("the aligner gave " + std::to_string(times.size()) + " times for " +
                     std::to_string(units.size()) + " words");
  }
  return times;
}

}  // namespace sillok::qwen
