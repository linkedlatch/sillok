#include "backends/qwen/qwen_aligner.hpp"

#include <algorithm>
#include <string>

#include <mtmd.h>

#include "core/model_provider.hpp"

namespace sillok::qwen {
namespace {

// The model's timestamp classes (config.json: classify_num,
// timestamp_segment_time), which llama.cpp's converter does not record.
constexpr int kClasses = 5000;
constexpr int kSegmentMs = 80;

}  // namespace

QwenAligner::QwenAligner(const std::filesystem::path& model, const std::filesystem::path& mmproj,
                         const ComputeDevice& device, int threads)
    : model_({.text_model = model,
              .mmproj = mmproj,
              .device = device,
              .threads = threads,
              // 180 s of audio is 2340 positions, and its words with their
              // slots take fewer than 6000.
              .context = 8192,
              .embeddings = false}),
      slot_(model_.Token("<timestamp>")) {
  if (model_.row_size() < kClasses) throw ModelError("not a Qwen3-ForcedAligner");
}

std::vector<int> QwenAligner::Align(std::span<const float> pcm, std::span<const AlignUnit> units,
                                    const std::function<void(double)>& on_progress,
                                    const StopToken& stop) {
  // The package's input: the audio, then each unit followed by two slots.
  std::string prompt = mtmd_default_marker();
  for (const AlignUnit& unit : units) prompt += unit.text + "<timestamp><timestamp>";
  std::vector<int> times;
  times.reserve(units.size() * 2);
  // The output layer is the classes of 80 ms; llama.cpp puts them at the end
  // of each row, after -inf up to the vocabulary's size.
  const int first = model_.row_size() - kClasses;
  model_.Feed(
      prompt, pcm, AudioTokens(pcm.size()), [&](llama_token token) { return token == slot_; },
      [&](llama_token, const float* row) {
        const float* classes = row + first;
        times.push_back(
            static_cast<int>(std::max_element(classes, classes + kClasses) - classes) * kSegmentMs);
      },
      false, on_progress, stop);
  if (times.size() != units.size() * 2) {
    throw ModelError("the aligner gave " + std::to_string(times.size()) + " times for " +
                     std::to_string(units.size()) + " words");
  }
  return times;
}

}  // namespace sillok::qwen
