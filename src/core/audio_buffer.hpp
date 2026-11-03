#ifndef SILLOK_CORE_AUDIO_BUFFER_HPP_
#define SILLOK_CORE_AUDIO_BUFFER_HPP_

#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "core/audio.hpp"

namespace sillok {

// Recording as 16 kHz mono 16-bit samples, kept in a temporary file so that
// memory use does not grow with the length of the recording; readers load
// only the range they need. media/audio_input.hpp converts other formats.
class AudioBuffer {
 public:
  // Creates the temporary file in `temp_dir`. Throws std::runtime_error if
  // that is not possible.
  explicit AudioBuffer(const std::filesystem::path& temp_dir =
                           std::filesystem::temp_directory_path());
  ~AudioBuffer();
  AudioBuffer(AudioBuffer&&) noexcept;
  AudioBuffer& operator=(AudioBuffer&&) noexcept;

  // Appends 16 kHz mono samples in [-1, 1]; louder ones are clipped.
  void Push(std::span<const float> samples);
  // Ends the recording. Push is not allowed afterwards.
  void Finish() { finished_ = true; }

  SampleIndex size() const;
  bool finished() const { return finished_; }

  // Samples in `range` (clamped to the buffer) as floats in [-1, 1).
  std::vector<float> Read(SampleRange range) const;
  // Mean square of consecutive frames of `frame` samples; the last frame may
  // be shorter.
  std::vector<float> FrameEnergy(int frame) const;

 private:
  class File;

  std::unique_ptr<File> file_;
  bool finished_ = false;
};

}  // namespace sillok

#endif  // SILLOK_CORE_AUDIO_BUFFER_HPP_
