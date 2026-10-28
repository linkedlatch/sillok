#ifndef SILLOK_CORE_AUDIO_BUFFER_HPP_
#define SILLOK_CORE_AUDIO_BUFFER_HPP_

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "core/audio.hpp"
#include "core/resampler.hpp"

namespace sillok {

// Recording converted to 16 kHz mono 16-bit samples and kept in a temporary
// file, so that memory use does not grow with the length of the recording;
// readers load only the range they need.
class AudioBuffer {
 public:
  // Creates the temporary file in `temp_dir`. Throws std::runtime_error if
  // that is not possible.
  explicit AudioBuffer(
      const std::filesystem::path& temp_dir = std::filesystem::temp_directory_path());
  ~AudioBuffer();
  AudioBuffer(AudioBuffer&&) noexcept;
  AudioBuffer& operator=(AudioBuffer&&) noexcept;

  // Appends `interleaved` (frames x channels values in [-1, 1]) recorded at
  // `sample_rate`. Rate and channel count must not change between calls.
  void Push(std::span<const float> interleaved, int sample_rate, int channels);
  // Flushes the resampler. Push is not allowed afterwards.
  void Finish();

  SampleIndex size() const;
  bool finished() const { return finished_; }

  // Samples in `range` (clamped to the buffer) as floats in [-1, 1).
  std::vector<float> Read(SampleRange range) const;
  // Mean square of consecutive frames of `frame` samples; the last frame may
  // be shorter.
  std::vector<float> FrameEnergy(int frame) const;

 private:
  class File;

  void Append(std::span<const float> mono);

  std::unique_ptr<File> file_;
  int sample_rate_ = 0;
  int channels_ = 0;
  bool finished_ = false;
  std::optional<Resampler> resampler_;
  std::vector<float> mono_;
  std::vector<float> resampled_;
};

}  // namespace sillok

#endif  // SILLOK_CORE_AUDIO_BUFFER_HPP_
