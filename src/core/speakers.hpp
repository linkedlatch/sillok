#ifndef SILLOK_CORE_SPEAKERS_HPP_
#define SILLOK_CORE_SPEAKERS_HPP_

#include <span>
#include <vector>

#include "core/audio.hpp"
#include "core/diarizer.hpp"
#include "core/transcript.hpp"

namespace sillok {

struct SpeakerOptions {
  // A word with no overlapping turn takes the nearest turn within this.
  Milliseconds max_distance{500};
};

// Labels every word with a speaker, splits segments where the speaker changes and numbers speakers 0, 1, ... in order
// of first appearance.
std::vector<Segment> AssignSpeakers(std::vector<Segment> segments,
                                    std::span<const SpeakerTurn> turns,
                                    const SpeakerOptions& options);

}  // namespace sillok

#endif  // SILLOK_CORE_SPEAKERS_HPP_
