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
  Millis max_distance{500};
  // A run of words shorter than this between turns of one other speaker is
  // treated as that speaker.
  Millis min_turn{1000};
};

// Labels every word with a speaker, smooths very short interjections, splits
// segments where the speaker changes and numbers speakers 0, 1, ... in order
// of first appearance.
std::vector<Segment> AssignSpeakers(std::vector<Segment> segments,
                                    std::span<const SpeakerTurn> turns,
                                    const SpeakerOptions& options);

}  // namespace sillok

#endif  // SILLOK_CORE_SPEAKERS_HPP_
