#ifndef SILLOK_CORE_RESAMPLER_HPP_
#define SILLOK_CORE_RESAMPLER_HPP_

#include <cstdint>
#include <span>
#include <vector>

namespace sillok {

// Streaming band-limited conversion from any rate in [8 kHz, 192 kHz] to
// kSampleRate, using a Kaiser-windowed sinc. Output sample n sits exactly at
// input position n * input_rate / kSampleRate, so timing never drifts.
class Resampler {
 public:
  explicit Resampler(int input_rate);

  // Converts the next block of input and appends the produced samples.
  void Process(std::span<const float> input, std::vector<float>& out);
  // Ends the stream and appends the remaining samples.
  void Finish(std::vector<float>& out);

 private:
  bool Passthrough() const { return up_ == 1 && down_ == 1; }
  std::size_t Taps() const { return static_cast<std::size_t>(2 * half_taps_); }
  // Produces outputs while their inputs are among the first `available` and
  // fewer than `limit` outputs exist.
  void Produce(std::int64_t available, std::int64_t limit,
               std::vector<float>& out);

  std::int64_t up_ = 1;    // output rate / gcd
  std::int64_t down_ = 1;  // input rate / gcd
  int half_taps_ = 0;
  std::vector<float> table_;  // up_ phases x Taps() coefficients
  std::vector<float> history_;
  std::int64_t history_start_ = 0;  // input index of history_[0]
  std::int64_t input_count_ = 0;
  std::int64_t output_count_ = 0;
};

}  // namespace sillok

#endif  // SILLOK_CORE_RESAMPLER_HPP_
