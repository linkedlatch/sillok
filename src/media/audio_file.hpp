#ifndef SILLOK_MEDIA_AUDIO_FILE_HPP_
#define SILLOK_MEDIA_AUDIO_FILE_HPP_

#include <filesystem>
#include <functional>
#include <stdexcept>

#include "core/audio_buffer.hpp"
#include "core/cancel.hpp"
#include "media/audio_input.hpp"

namespace sillok {

// A file that cannot be opened, or has no audio sillok can decode.
class MediaError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Decodes the first audio stream of the file at `path` (with FFmpeg) and
// pushes it into `input` in the file's own rate and channels, from the start
// of the file and without the encoder's priming samples. `progress` receives
// the fraction of the file read.
void DecodeAudioFile(const std::filesystem::path& path, AudioInput& input,
                     const std::function<void(double)>& progress,
                     const StopToken& stop);

// Writes `audio` (16 kHz mono, finished) to `path` as AAC-LC at 64 kbit/s in an M4A file, whose edit list hides the encoder's
// priming so that positions in it match the recording's. `progress` receives
// the fraction written. A partly written file is removed.
void WriteAacFile(const AudioBuffer& audio, const std::filesystem::path& path,
                  const std::function<void(double)>& progress,
                  const StopToken& stop);

}  // namespace sillok

#endif  // SILLOK_MEDIA_AUDIO_FILE_HPP_
