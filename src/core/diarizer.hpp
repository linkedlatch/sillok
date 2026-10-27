#ifndef SILLOK_CORE_DIARIZER_HPP_
#define SILLOK_CORE_DIARIZER_HPP_

#include <span>
#include <vector>

#include "core/audio.hpp"
#include "core/transcript.hpp"

namespace sillok {

struct SpeakerTurn {
  SampleRange range;
  int speaker = 0;  // model label, 0-based
};

// Who spoke when, for one recording fed in order.
class Diarizer {
 public:
  virtual ~Diarizer() = default;

  virtual ModelInfo Info() const = 0;
  // Feeds the next part of the recording (16 kHz mono).
  virtual void Push(std::span<const float> pcm) = 0;
  // Ends the recording. Turns of different speakers may overlap.
  virtual std::vector<SpeakerTurn> Finish() = 0;
};

}  // namespace sillok

#endif  // SILLOK_CORE_DIARIZER_HPP_
