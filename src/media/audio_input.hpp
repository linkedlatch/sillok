#ifndef SILLOK_MEDIA_AUDIO_INPUT_HPP_
#define SILLOK_MEDIA_AUDIO_INPUT_HPP_

#include <memory>
#include <span>
#include <vector>

#include "core/audio_buffer.hpp"

struct SwrContext;

namespace sillok {

// Brings audio of any rate in [8 kHz, 192 kHz] and any channel count to the
// 16 kHz mono of an AudioBuffer: the channels are averaged, and FFmpeg's
// libswresample changes the rate. Output sample n sits at input position
// n * input_rate / kSampleRate, and the output lasts as long as the input.
class AudioInput {
 public:
  explicit AudioInput(AudioBuffer& audio);
  ~AudioInput();
  AudioInput(const AudioInput&) = delete;
  AudioInput& operator=(const AudioInput&) = delete;

  // Appends `interleaved` (frames x channels values in [-1, 1]) recorded at
  // `sample_rate`. Rate and channel count must not change between calls.
  void Push(std::span<const float> interleaved, int sample_rate, int channels);
  // Flushes the resampler and finishes the buffer. Push is not allowed
  // afterwards.
  void Finish();

 private:
  struct SwrFree {
    void operator()(SwrContext* context) const;
  };

  // Converts `mono` (nullptr to flush) and appends the result.
  void Convert(const float* mono, int count);

  AudioBuffer& audio_;
  std::unique_ptr<SwrContext, SwrFree> swr_;
  int sample_rate_ = 0;
  int channels_ = 0;
  std::vector<float> mono_;
  std::vector<float> resampled_;
};

}  // namespace sillok

#endif  // SILLOK_MEDIA_AUDIO_INPUT_HPP_
