#ifndef SILLOK_MEDIA_AUDIO_FILE_HPP_
#define SILLOK_MEDIA_AUDIO_FILE_HPP_

#include <filesystem>
#include <functional>
#include <stdexcept>

#include "core/audio_buffer.hpp"
#include "core/cancel.hpp"

namespace sillok {

// A file that cannot be opened, or has no audio sillok can decode.
class MediaError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Decodes the first audio stream of the file at `path` (with FFmpeg) and
// pushes it into `audio` in the file's own rate and channels, from the start
// of the file and without the encoder's priming samples. `progress` receives
// the fraction of the file read.
void DecodeAudioFile(const std::filesystem::path& path, AudioBuffer& audio,
                     const std::function<void(double)>& progress,
                     const StopToken& stop);

}  // namespace sillok

#endif  // SILLOK_MEDIA_AUDIO_FILE_HPP_
