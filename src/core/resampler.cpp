#include "core/resampler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>

#include "core/audio.hpp"

namespace sillok {
namespace {

constexpr int kZeroCrossings = 16;
constexpr double kRolloff = 0.94;
constexpr double kKaiserBeta = 8.0;
// Consumed history is dropped once it grows past this many samples.
constexpr std::int64_t kCompactThreshold = std::int64_t{1} << 16;

double BesselI0(double x) {
  double sum = 1.0;
  double term = 1.0;
  const double half = x / 2.0;
  for (int k = 1; k < 64; ++k) {
    term *= (half / k) * (half / k);
    sum += term;
    if (term < sum * 1e-15) break;
  }
  return sum;
}

// Low-pass kernel with `cutoff` in cycles per input sample, windowed to
// |t| < half_width input samples.
double WindowedSinc(double t, double cutoff, int half_width) {
  const double x = t / half_width;
  if (x <= -1.0 || x >= 1.0) return 0.0;
  const double arg = 2.0 * cutoff * t;
  const double sinc =
      arg == 0.0 ? 1.0
                 : std::sin(std::numbers::pi * arg) / (std::numbers::pi * arg);
  static const double kWindowScale = 1.0 / BesselI0(kKaiserBeta);
  return 2.0 * cutoff * sinc * BesselI0(kKaiserBeta * std::sqrt(1.0 - x * x)) *
         kWindowScale;
}

}  // namespace

Resampler::Resampler(int input_rate) {
  if (input_rate < 8000 || input_rate > 192000) {
    throw std::invalid_argument("unsupported sample rate " +
                                std::to_string(input_rate));
  }
  const int divisor = std::gcd(input_rate, kSampleRate);
  up_ = kSampleRate / divisor;
  down_ = input_rate / divisor;
  if (Passthrough()) return;

  const double cutoff =
      0.5 * kRolloff *
      std::min(1.0, static_cast<double>(up_) / static_cast<double>(down_));
  half_taps_ = static_cast<int>(std::ceil(kZeroCrossings / (2.0 * cutoff)));

  // Output n is centered at input position base + phase / up_; its taps are
  // inputs base - half_taps_ + 1 ... base + half_taps_.
  const std::size_t taps = Taps();
  table_.resize(static_cast<std::size_t>(up_) * taps);
  std::vector<double> row(taps);
  for (std::int64_t phase = 0; phase < up_; ++phase) {
    const double fraction =
        static_cast<double>(phase) / static_cast<double>(up_);
    double sum = 0.0;
    for (std::size_t j = 0; j < taps; ++j) {
      row[j] = WindowedSinc(fraction + half_taps_ - 1 - static_cast<double>(j),
                            cutoff, half_taps_);
      sum += row[j];
    }
    // Unit gain at DC for every phase.
    float* out = table_.data() + static_cast<std::size_t>(phase) * taps;
    for (std::size_t j = 0; j < taps; ++j) {
      out[j] = static_cast<float>(row[j] / sum);
    }
  }
  // Input before the start of the stream is silence.
  history_.assign(static_cast<std::size_t>(half_taps_), 0.0f);
  history_start_ = -half_taps_;
}

void Resampler::Process(std::span<const float> input,
                        std::vector<float>& out) {
  if (Passthrough()) {
    out.insert(out.end(), input.begin(), input.end());
    return;
  }
  history_.insert(history_.end(), input.begin(), input.end());
  input_count_ += static_cast<std::int64_t>(input.size());
  Produce(input_count_, std::numeric_limits<std::int64_t>::max(), out);

  const std::int64_t first_needed =
      output_count_ * down_ / up_ - half_taps_ + 1;
  if (first_needed - history_start_ > kCompactThreshold) {
    history_.erase(history_.begin(),
                   history_.begin() + (first_needed - history_start_));
    history_start_ = first_needed;
  }
}

void Resampler::Finish(std::vector<float>& out) {
  if (Passthrough()) return;
  // The output covers exactly the duration of the input.
  const std::int64_t total = (input_count_ * up_ + down_ - 1) / down_;
  history_.insert(history_.end(), Taps(), 0.0f);
  Produce(input_count_ + 2 * half_taps_, total, out);
}

void Resampler::Produce(std::int64_t available, std::int64_t limit,
                        std::vector<float>& out) {
  const std::size_t taps = Taps();
  while (output_count_ < limit) {
    const std::int64_t position = output_count_ * down_;
    const std::int64_t base = position / up_;
    if (base + half_taps_ >= available) break;
    const float* x =
        history_.data() + (base - half_taps_ + 1 - history_start_);
    const float* c =
        table_.data() + static_cast<std::size_t>(position % up_) * taps;
    float sum = 0.0f;
    for (std::size_t j = 0; j < taps; ++j) sum += c[j] * x[j];
    out.push_back(sum);
    ++output_count_;
  }
}

}  // namespace sillok
