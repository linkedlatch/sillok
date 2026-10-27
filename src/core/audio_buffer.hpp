#ifndef SILLOK_CORE_AUDIO_BUFFER_HPP_
#define SILLOK_CORE_AUDIO_BUFFER_HPP_

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/audio.hpp"
#include "core/resampler.hpp"

namespace sillok {

// Recording converted to 16 kHz mono and stored as 16-bit samples. Storage is
// split into fixed blocks so that growing a long recording never copies it.
class AudioBuffer {
 public:
  // Appends `interleaved` (frames x channels values in [-1, 1]) recorded at
  // `sample_rate`. Rate and channel count must not change between calls.
  void Push(std::span<const float> interleaved, int sample_rate, int channels);
  // Flushes the resampler. Push is not allowed afterwards.
  void Finish();

  SampleIndex size() const { return size_; }
  bool finished() const { return finished_; }

  // Samples in `range` (clamped to the buffer) as floats in [-1, 1).
  std::vector<float> Read(SampleRange range) const;
  // Mean square of consecutive frames of `frame` samples; the last frame may
  // be shorter.
  std::vector<float> FrameEnergy(int frame) const;

 private:
  void Append(std::span<const float> mono);
  std::int16_t At(SampleIndex index) const;

  std::vector<std::vector<std::int16_t>> blocks_;
  SampleIndex size_ = 0;
  int sample_rate_ = 0;
  int channels_ = 0;
  bool finished_ = false;
  std::optional<Resampler> resampler_;
  std::vector<float> mono_;
  std::vector<float> resampled_;
};

}  // namespace sillok

#endif  // SILLOK_CORE_AUDIO_BUFFER_HPP_
