#include "core/level.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "core/audio.hpp"

namespace sillok {
namespace {

constexpr int kBlockSamples =
    kSampleRate * 4 / 10;               // 400 ms, R 128's momentary loudness
constexpr double kAbsoluteGate = 1e-7;  // -70 dBFS, in energy
constexpr double kRelativeGate =
    0.1;  // 10 dB below the mean of the blocks above it
constexpr double kTargetRms =
    0.05;                          // -26 dBFS, the nominal level of codec tests
constexpr double kMaxGain = 31.6;  // 30 dB
constexpr double kKnee = 0.9;

// Mean of the energies above `gate`; 0 without any.
double MeanAbove(const std::vector<float>& energy, double gate) {
  double sum = 0.0;
  std::size_t count = 0;
  for (const float e : energy) {
    if (static_cast<double>(e) > gate) {
      sum += static_cast<double>(e);
      ++count;
    }
  }
  return count == 0 ? 0.0 : sum / static_cast<double>(count);
}

}  // namespace

double LoudnessGain(const AudioBuffer& audio) {
  const std::vector<float> energy = audio.FrameEnergy(kBlockSamples);
  const double ungated = MeanAbove(energy, kAbsoluteGate);
  if (ungated <= 0.0) return 1.0;  // silence
  const double speech =
      MeanAbove(energy, std::max(kAbsoluteGate, ungated * kRelativeGate));
  return std::clamp(kTargetRms / std::sqrt(speech), 1.0, kMaxGain);
}

void ApplyGain(double gain, std::span<float> pcm) {
  if (gain == 1.0) return;
  for (float& sample : pcm) {
    const double amplified = static_cast<double>(sample) * gain;
    const double magnitude = std::abs(amplified);
    const double bent =
        magnitude <= kKnee
            ? magnitude
            : kKnee + (1.0 - kKnee) *
                          std::tanh((magnitude - kKnee) / (1.0 - kKnee));
    sample = static_cast<float>(std::copysign(bent, amplified));
  }
}

}  // namespace sillok
