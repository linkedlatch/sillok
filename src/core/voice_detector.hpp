#ifndef SILLOK_CORE_VOICE_DETECTOR_HPP_
#define SILLOK_CORE_VOICE_DETECTOR_HPP_

#include <span>
#include <vector>

#include "core/transcript.hpp"

namespace sillok {

// Frame-level speech probabilities.
class VoiceDetector {
 public:
  virtual ~VoiceDetector() = default;

  virtual ModelInfo Info() const = 0;
  // Samples summarized by one probability.
  virtual int FrameSamples() const = 0;
  // Speech probability of each consecutive frame of `pcm` (16 kHz mono).
  // State carries over between calls: a recording is fed in order, in pieces
  // whose length is a multiple of FrameSamples() except for the last one.
  virtual std::vector<float> Detect(std::span<const float> pcm) = 0;
};

}  // namespace sillok

#endif  // SILLOK_CORE_VOICE_DETECTOR_HPP_
