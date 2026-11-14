#include "backends/qwen/qwen_chunks.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace sillok::qwen {
namespace {

// Where in `samples` the cut falls: the quietest sample of the first of the
// quietest windows (numpy's argmin of the moving sum, then within it).
SampleIndex QuietestSample(const std::vector<float>& samples) {
  const auto at = [&](SampleIndex i) { return std::abs(samples[static_cast<std::size_t>(i)]); };
  const auto n = static_cast<SampleIndex>(samples.size());
  double sum = 0.0;
  for (SampleIndex i = 0; i < kCutWindow; ++i) sum += at(i);
  double quietest = sum;
  SampleIndex window = 0;
  for (SampleIndex i = kCutWindow; i < n; ++i) {
    sum += at(i) - at(i - kCutWindow);
    if (sum < quietest) {
      quietest = sum;
      window = i - kCutWindow + 1;
    }
  }
  SampleIndex inner = 0;
  for (SampleIndex i = 1; i < kCutWindow; ++i) {
    if (at(window + i) < at(window + inner)) inner = i;
  }
  return window + inner;
}

}  // namespace

std::vector<SampleRange> AudioChunks(const AudioBuffer& audio) {
  const SampleIndex total = audio.size();
  std::vector<SampleRange> chunks;
  SampleIndex start = 0;
  while (total - start > kMaxChunk) {
    const SampleIndex mark = start + kMaxChunk;
    const SampleIndex left = std::max(start, mark - kCutSearch);
    const SampleIndex right = std::min(total, mark + kCutSearch);
    const SampleIndex cut =
        right - left <= kCutWindow ? mark : left + QuietestSample(audio.Read({left, right}));
    const SampleIndex end = std::clamp(cut, start + 1, total);
    chunks.push_back({start, end});
    start = end;
  }
  chunks.push_back({start, total});
  return chunks;
}

}  // namespace sillok::qwen
