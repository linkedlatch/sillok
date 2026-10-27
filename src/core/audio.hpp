#ifndef SILLOK_CORE_AUDIO_HPP_
#define SILLOK_CORE_AUDIO_HPP_

#include <chrono>
#include <cstdint>

namespace sillok {

// Audio inside sillok is 16 kHz mono; positions are sample indices at that rate.
inline constexpr int kSampleRate = 16000;

using SampleIndex = std::int64_t;
using Millis = std::chrono::milliseconds;

constexpr SampleIndex ToSamples(Millis time) {
  return time.count() * kSampleRate / 1000;
}

constexpr Millis ToMillis(SampleIndex samples) {
  return Millis{samples * 1000 / kSampleRate};
}

// Half-open interval [begin, end) of samples.
struct SampleRange {
  SampleIndex begin = 0;
  SampleIndex end = 0;

  constexpr SampleIndex length() const { return end - begin; }
  friend constexpr bool operator==(const SampleRange&,
                                   const SampleRange&) = default;
};

}  // namespace sillok

#endif  // SILLOK_CORE_AUDIO_HPP_
