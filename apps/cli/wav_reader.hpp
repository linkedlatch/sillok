#ifndef SILLOK_APPS_CLI_WAV_READER_HPP_
#define SILLOK_APPS_CLI_WAV_READER_HPP_

#include <cstdint>
#include <cstdio>
#include <vector>

namespace sillok::cli {

// Streaming reader of RIFF/WAVE with 16-bit integer or 32-bit float PCM.
// Also reads WAV written to a pipe, whose size fields are unknown.
class WavReader {
 public:
  // Reads the header from `file` (not owned). Throws std::runtime_error if it
  // is not a supported WAV stream.
  explicit WavReader(std::FILE* file);

  int sample_rate() const { return sample_rate_; }
  int channels() const { return channels_; }
  bool is_float() const { return format_ == 3; }
  int bits() const { return bits_; }

  // Reads up to `frames` frames as interleaved floats in [-1, 1]. Returns an
  // empty vector at the end of the data.
  std::vector<float> Read(std::size_t frames);

 private:
  std::FILE* file_;
  int format_ = 0;  // 1 = PCM, 3 = IEEE float
  int sample_rate_ = 0;
  int channels_ = 0;
  int bits_ = 0;
  std::uint64_t remaining_ = 0;  // data bytes left; UINT64_MAX if unknown
  std::vector<unsigned char> buffer_;
};

}  // namespace sillok::cli

#endif  // SILLOK_APPS_CLI_WAV_READER_HPP_
