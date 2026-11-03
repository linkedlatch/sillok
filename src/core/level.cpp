#include "core/level.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sillok {
namespace {

constexpr std::size_t kFrame = 3200;      // 200 ms at 16 kHz
constexpr std::size_t kNeighbors = 5;     // frames on each side: about 2.2 s in all
constexpr double kTargetRms = 0.05;       // -26 dBFS
constexpr double kMaxGain = 31.6;         // 30 dB
constexpr double kMaxPeak = 0.95;
// Frames that set the gain, as speech: 6 dB above the audio's noise floor
// (its quietest tenth of frames) and above -70 dBFS, EBU R 128's absolute
// gate. Pauses in a lecture are only about 10 dB below the speech; measured
// with them, the gain raised their noise to almost the speech's level.
constexpr double kFloorQuantile = 0.1;
constexpr double kAboveFloor = 4.0;       // 6 dB, in energy
constexpr double kMinEnergy = 1e-7;       // -70 dBFS

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

  const auto length = [&](std::size_t f) { return std::min(kFrame, pcm.size() - f * kFrame); };
  std::vector<double> mean(frames);
  for (std::size_t f = 0; f < frames; ++f) mean[f] = energy[f] / static_cast<double>(length(f));
  std::vector<double> sorted = mean;
  const auto quantile = sorted.begin() + static_cast<std::ptrdiff_t>(kFloorQuantile * static_cast<double>(frames - 1));
  std::nth_element(sorted.begin(), quantile, sorted.end());
  const double gate = std::max(kMinEnergy, *quantile * kAboveFloor);

  // Gain at the center of each frame from the speech in its neighborhood.
  std::vector<double> gain(frames, 0.0);  // 0: no speech nearby
  std::vector<double> loudest(frames, 0.0);
  for (std::size_t f = 0; f < frames; ++f) {
    const std::size_t first = f >= kNeighbors ? f - kNeighbors : 0;
    const std::size_t last = std::min(frames, f + kNeighbors + 1);
    double sum = 0.0;
    std::size_t samples = 0;
    for (std::size_t k = first; k < last; ++k) {
      loudest[f] = std::max(loudest[f], peak[k]);
      if (mean[k] <= gate) continue;
      sum += energy[k];
      samples += length(k);
    }
    if (samples > 0) gain[f] = kTargetRms / std::sqrt(sum / static_cast<double>(samples));
  }
  // A longer pause keeps the lower gain of the speech on either side, so that
  // its noise stays as far below the speech as it was.
  std::vector<double> before(frames, 0.0);
  for (std::size_t f = 0; f < frames; ++f) {
    before[f] = gain[f] > 0.0 ? gain[f] : f > 0 ? before[f - 1] : 0.0;
  }
  double after = 0.0;
  for (std::size_t f = frames; f-- > 0;) {
    if (gain[f] > 0.0) {
      after = gain[f];
    } else {
      const double held = before[f] > 0.0 && after > 0.0 ? std::min(before[f], after)
                                                         : std::max(before[f], after);
      gain[f] = held > 0.0 ? held : 1.0;  // no speech at all: left as it is
    }
  }
  for (std::size_t f = 0; f < frames; ++f) {
    if (loudest[f] > 0.0) gain[f] = std::min(gain[f], kMaxPeak / loudest[f]);
    gain[f] = std::clamp(gain[f], 1.0, kMaxGain);
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
