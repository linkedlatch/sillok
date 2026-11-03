#ifndef SILLOK_CORE_LEVEL_HPP_
#define SILLOK_CORE_LEVEL_HPP_

#include <span>

#include "core/audio_buffer.hpp"

namespace sillok {

// One gain for a whole recording that brings its speech to -26 dBFS (the
// nominal speech level of ITU-T and 3GPP codec tests), from 0 to 30 dB;
// louder recordings are left as they are. Voice detection and Whisper miss
// speech recorded near -50 dBFS. The speech level is measured as EBU R 128
// gates loudness: 400 ms blocks above -70 dBFS, then those within 10 dB of
// their mean, so pauses do not count. One gain keeps every passage, and the
// noise of the pauses, where it was relative to the speech.
double LoudnessGain(const AudioBuffer& audio);

// Multiplies `pcm` by `gain`; the rare peaks it would push past 0.9 (a knock
// on the desk) are bent towards 1 instead of clipped. A gain of 1 leaves the
// samples as they are.
void ApplyGain(double gain, std::span<float> pcm);

}  // namespace sillok

#endif  // SILLOK_CORE_LEVEL_HPP_
