#include "core/audio_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace sillok {
namespace {

// Room taken at the first push, a minute, so that short recordings never
// grow and long ones grow from there.
constexpr std::size_t kFirstReserve = std::size_t{kSampleRate} * 60;

// The one place all audio passes: NaN becomes silence, infinities clip.
std::int16_t ToInt16(float value) {
  if (std::isnan(value)) return 0;
  const float scaled = std::round(value * 32768.0f);
  return static_cast<std::int16_t>(std::clamp(scaled, -32768.0f, 32767.0f));
}

}  // namespace

void AudioBuffer::Push(std::span<const float> samples) {
  if (finished_) throw std::invalid_argument("audio already finished");
  if (samples_.capacity() == 0) samples_.reserve(kFirstReserve);
  const std::size_t from = samples_.size();
  samples_.resize(from + samples.size());
  std::transform(samples.begin(), samples.end(),
                 samples_.begin() + static_cast<std::ptrdiff_t>(from), ToInt16);
}

std::vector<float> AudioBuffer::Read(SampleRange range) const {
  const SampleIndex begin = std::clamp<SampleIndex>(range.begin, 0, size());
  const SampleIndex end = std::clamp<SampleIndex>(range.end, begin, size());
  std::vector<float> out(static_cast<std::size_t>(end - begin));
  std::transform(samples_.begin() + static_cast<std::ptrdiff_t>(begin),
                 samples_.begin() + static_cast<std::ptrdiff_t>(end), out.begin(),
                 [](std::int16_t s) { return static_cast<float>(s) / 32768.0f; });
  return out;
}

}  // namespace sillok
