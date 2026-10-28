#include "core/level.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sillok {
namespace {

constexpr std::size_t kFrame = 3200;      // 200 ms at 16 kHz
constexpr std::size_t kNeighbors = 5;     // frames on each side: about 2.2 s in all
constexpr double kTargetRms = 0.05;       // -26 dBFS
constexpr double kMaxGain = 31.6;         // 30 dB
constexpr double kMaxPeak = 0.95;

}  // namespace

void NormalizeLevel(std::span<float> pcm) {
  const std::size_t frames = (pcm.size() + kFrame - 1) / kFrame;
  if (frames == 0) return;
  std::vector<double> energy(frames, 0.0);
  std::vector<double> peak(frames, 0.0);
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    const auto x = static_cast<double>(pcm[i]);
    energy[i / kFrame] += x * x;
    peak[i / kFrame] = std::max(peak[i / kFrame], std::abs(x));
  }

  // Gain at the center of each frame from its neighborhood.
  std::vector<double> gain(frames, 1.0);
  for (std::size_t f = 0; f < frames; ++f) {
    const std::size_t first = f >= kNeighbors ? f - kNeighbors : 0;
    const std::size_t last = std::min(frames, f + kNeighbors + 1);
    double sum = 0.0;
    double loudest = 0.0;
    std::size_t samples = 0;
    for (std::size_t k = first; k < last; ++k) {
      sum += energy[k];
      loudest = std::max(loudest, peak[k]);
      samples += std::min(kFrame, pcm.size() - k * kFrame);
    }
    const double rms = std::sqrt(sum / static_cast<double>(samples));
    double g = rms > 0.0 ? kTargetRms / rms : kMaxGain;
    if (loudest > 0.0) g = std::min(g, kMaxPeak / loudest);
    gain[f] = std::clamp(g, 1.0, kMaxGain);
  }

  // Linear interpolation between frame centers avoids audible steps.
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    const double position =
        (static_cast<double>(i) + 0.5) / static_cast<double>(kFrame) - 0.5;
    const auto left = static_cast<std::size_t>(std::clamp(position, 0.0, static_cast<double>(frames - 1)));
    const std::size_t right = std::min(left + 1, frames - 1);
    const double t = std::clamp(position - static_cast<double>(left), 0.0, 1.0);
    const double g = gain[left] + (gain[right] - gain[left]) * t;
    pcm[i] = static_cast<float>(std::clamp(static_cast<double>(pcm[i]) * g, -1.0, 1.0));
  }
}

}  // namespace sillok
