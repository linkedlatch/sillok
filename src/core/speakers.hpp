#ifndef SILLOK_CORE_SPEAKERS_HPP_
#define SILLOK_CORE_SPEAKERS_HPP_

#include <span>
#include <vector>

#include "core/audio.hpp"
#include "core/diarizer.hpp"
#include "core/transcript.hpp"

namespace sillok {

// Labels every word with the speaker whose turns overlap it the most, or with
// no turn overlapping, the speaker of the nearest turn. Splits segments where
// the speaker changes and numbers speakers 0, 1, ... in order of first
// appearance.
std::vector<Segment> AssignSpeakers(std::vector<Segment> segments,
                                    std::span<const SpeakerTurn> turns);

}  // namespace sillok

#endif  // SILLOK_CORE_SPEAKERS_HPP_
