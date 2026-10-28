#include "wav_reader.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace sillok::cli {
namespace {

constexpr std::uint64_t kUnknownSize = std::numeric_limits<std::uint64_t>::max();

std::uint32_t Le32(const unsigned char* p) {
  return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
         static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

std::uint16_t Le16(const unsigned char* p) {
  return static_cast<std::uint16_t>(p[0] | p[1] << 8);
}

void ReadExactly(std::FILE* file, unsigned char* out, std::size_t size) {
  if (std::fread(out, 1, size, file) != size) {
    throw std::runtime_error("truncated WAV header");
  }
}

void Skip(std::FILE* file, std::uint64_t size) {
  unsigned char scratch[4096];
  while (size > 0) {
    const auto step = static_cast<std::size_t>(std::min<std::uint64_t>(size, sizeof scratch));
    ReadExactly(file, scratch, step);
    size -= step;
  }
}

}  // namespace

WavReader::WavReader(std::FILE* file) : file_(file) {
  unsigned char riff[12];
  ReadExactly(file_, riff, sizeof riff);
  if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) {
    throw std::runtime_error("not a WAV file");
  }
  bool have_format = false;
  while (true) {
    unsigned char chunk[8];
    ReadExactly(file_, chunk, sizeof chunk);
    const std::uint32_t size = Le32(chunk + 4);
    if (std::memcmp(chunk, "fmt ", 4) == 0) {
      if (size < 16) throw std::runtime_error("bad WAV format chunk");
      std::vector<unsigned char> fmt(size + (size & 1));
      ReadExactly(file_, fmt.data(), fmt.size());
      format_ = Le16(fmt.data());
      channels_ = Le16(fmt.data() + 2);
      sample_rate_ = static_cast<int>(Le32(fmt.data() + 4));
      bits_ = Le16(fmt.data() + 14);
      if (format_ == 0xFFFE && size >= 26) format_ = Le16(fmt.data() + 24);  // extensible
      have_format = true;
    } else if (std::memcmp(chunk, "data", 4) == 0) {
      if (!have_format) throw std::runtime_error("WAV data before format");
      // Streams written to a pipe leave the size at 0 or 0xFFFFFFFF.
      remaining_ = size == 0 || size == 0xFFFFFFFF ? kUnknownSize : size;
      break;
    } else {
      Skip(file_, size + (size & 1));
    }
  }
  const bool pcm16 = format_ == 1 && bits_ == 16;
  const bool float32 = format_ == 3 && bits_ == 32;
  if (!pcm16 && !float32) {
    throw std::runtime_error("unsupported WAV encoding (format " +
                             std::to_string(format_) + ", " +
                             std::to_string(bits_) + " bits)");
  }
  if (channels_ < 1 || sample_rate_ <= 0) throw std::runtime_error("bad WAV format");
}

std::vector<float> WavReader::Read(std::size_t frames) {
  const std::size_t sample_bytes = static_cast<std::size_t>(bits_ / 8);
  const std::size_t frame_bytes = sample_bytes * static_cast<std::size_t>(channels_);
  std::uint64_t wanted = frames * frame_bytes;
  if (remaining_ != kUnknownSize) wanted = std::min(wanted, remaining_);
  buffer_.resize(static_cast<std::size_t>(wanted));
  std::size_t got = std::fread(buffer_.data(), 1, buffer_.size(), file_);
  got -= got % frame_bytes;  // a partial frame at the very end is dropped
  if (remaining_ != kUnknownSize) remaining_ -= got;

  std::vector<float> out(got / sample_bytes);
  for (std::size_t i = 0; i < out.size(); ++i) {
    const unsigned char* p = buffer_.data() + i * sample_bytes;
    if (is_float()) {
      const std::uint32_t bits = Le32(p);
      float value;
      std::memcpy(&value, &bits, sizeof value);
      out[i] = value;
    } else {
      out[i] = static_cast<float>(static_cast<std::int16_t>(Le16(p))) / 32768.0f;
    }
  }
  return out;
}

}  // namespace sillok::cli
