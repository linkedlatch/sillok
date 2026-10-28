#ifndef SILLOK_CORE_LEVEL_HPP_
#define SILLOK_CORE_LEVEL_HPP_

#include <span>

namespace sillok {

// Raises quiet passages of 16 kHz audio towards a common level; never lowers
// any. Voice detection and Whisper miss speech recorded tens of decibels
// below the rest of a recording, such as a question from the back of a
// lecture hall. The gain follows the local level over about two seconds, is
// limited to 30 dB so that silence stays quiet, and keeps peaks below 0.95.
void NormalizeLevel(std::span<float> pcm);

}  // namespace sillok

#endif  // SILLOK_CORE_LEVEL_HPP_
