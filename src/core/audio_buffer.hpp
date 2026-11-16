#ifndef SILLOK_CORE_AUDIO_BUFFER_HPP_
#define SILLOK_CORE_AUDIO_BUFFER_HPP_

#include <cstdint>
#include <span>
#include <vector>

#include "core/audio.hpp"

namespace sillok {

// Recording as 16 kHz mono 16-bit samples in memory, 115 MB an hour: little
// next to the models, and half of what floats would take.
// media/audio_input.hpp converts other formats.
class AudioBuffer {
 public:
  AudioBuffer() = default;
  // Move-only: a copy of a long recording is never wanted.
  AudioBuffer(const AudioBuffer&) = delete;
  AudioBuffer& operator=(const AudioBuffer&) = delete;
  AudioBuffer(AudioBuffer&&) noexcept = default;
  AudioBuffer& operator=(AudioBuffer&&) noexcept = default;

  // Appends 16 kHz mono samples in [-1, 1]; louder ones are clipped. Throws
  // std::bad_alloc when memory runs out.
  void Push(std::span<const float> samples);
  // Ends the recording. Push is not allowed afterwards.
  void Finish() { finished_ = true; }

  SampleIndex size() const { return static_cast<SampleIndex>(samples_.size()); }
  bool finished() const { return finished_; }

  // Samples in `range` (clamped to the buffer) as floats in [-1, 1). Several
  // threads may read at once, but not while one pushes.
  std::vector<float> Read(SampleRange range) const;

 private:
  std::vector<std::int16_t> samples_;
  bool finished_ = false;
};

}  // namespace sillok

#endif  // SILLOK_CORE_AUDIO_BUFFER_HPP_
