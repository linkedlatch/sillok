#include "core/audio_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "core/files.hpp"

namespace sillok {
namespace {

constexpr std::size_t kWriteBufferSamples = std::size_t{1} << 16;
constexpr std::string_view kPrefix = "sillok-audio-";
constexpr std::string_view kExtension = ".pcm";

std::int16_t ToInt16(float value) {
  const float scaled = std::round(value * 32768.0f);
  return static_cast<std::int16_t>(std::clamp(scaled, -32768.0f, 32767.0f));
}

std::string UniqueName() {
  std::random_device device;
  const std::uint64_t value =
      static_cast<std::uint64_t>(device()) << 32 | device();
  constexpr char kHex[] = "0123456789abcdef";
  std::string name(kPrefix);
  for (int shift = 60; shift >= 0; shift -= 4)
    name.push_back(kHex[(value >> shift) & 0xF]);
  return name.append(kExtension);
}

// Files left by processes that ended without cleaning up. A file still in
// use cannot be removed on Windows, and elsewhere files in use have no name
// (see AudioBuffer::File), so only leftovers go.
void RemoveLeftovers(const std::filesystem::path& dir) {
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(dir, error)) {
    const std::string name = PathToUtf8(entry.path().filename());
    if (name.starts_with(kPrefix) && name.ends_with(kExtension)) {
      std::filesystem::remove(entry.path(), error);
    }
  }
}

}  // namespace

// Temporary file of 16-bit samples with a small write buffer.
class AudioBuffer::File {
 public:
  explicit File(const std::filesystem::path& dir) : path_(dir / UniqueName()) {
    RemoveLeftovers(dir);
    stream_.open(path_, std::ios::binary | std::ios::in | std::ios::out |
                            std::ios::trunc);
    if (!stream_) {
      throw std::runtime_error("cannot create a temporary file in " +
                               PathToUtf8(dir));
    }
    // On POSIX systems the open file stays usable without a name and goes
    // away by itself however the process ends. Windows refuses; there the
    // file is removed when the buffer is destroyed.
    std::error_code error;
    named_ = !std::filesystem::remove(path_, error);
    pending_.reserve(kWriteBufferSamples);
  }

  ~File() {
    stream_.close();
    if (named_) {
      std::error_code error;
      std::filesystem::remove(path_, error);
    }
  }

  File(const File&) = delete;
  File& operator=(const File&) = delete;

  SampleIndex size() const { return size_; }

  void Append(std::int16_t sample) {
    pending_.push_back(sample);
    ++size_;
    if (pending_.size() == kWriteBufferSamples) Flush();
  }

  void Read(SampleIndex begin, std::span<std::int16_t> out) {
    Flush();
    stream_.seekg(static_cast<std::streamoff>(begin) * 2);
    stream_.read(reinterpret_cast<char*>(out.data()),
                 static_cast<std::streamsize>(out.size_bytes()));
    if (!stream_)
      throw std::runtime_error("cannot read the temporary audio file");
  }

 private:
  void Flush() {
    if (pending_.empty()) return;
    stream_.seekp(0, std::ios::end);
    stream_.write(
        reinterpret_cast<const char*>(pending_.data()),
        static_cast<std::streamsize>(pending_.size() * sizeof(std::int16_t)));
    if (!stream_)
      throw std::runtime_error("cannot write the temporary audio file");
    pending_.clear();
  }

  std::filesystem::path path_;
  bool named_ = true;
  std::fstream stream_;
  std::vector<std::int16_t> pending_;
  SampleIndex size_ = 0;
};

AudioBuffer::AudioBuffer(const std::filesystem::path& temp_dir)
    : file_(std::make_unique<File>(temp_dir)) {}

AudioBuffer::~AudioBuffer() = default;
AudioBuffer::AudioBuffer(AudioBuffer&&) noexcept = default;
AudioBuffer& AudioBuffer::operator=(AudioBuffer&&) noexcept = default;

SampleIndex AudioBuffer::size() const { return file_->size(); }

void AudioBuffer::Push(std::span<const float> samples) {
  if (finished_) throw std::logic_error("audio already finished");
  for (const float value : samples) file_->Append(ToInt16(value));
}

std::vector<float> AudioBuffer::Read(SampleRange range) const {
  const SampleIndex begin = std::clamp<SampleIndex>(range.begin, 0, size());
  const SampleIndex end = std::clamp<SampleIndex>(range.end, begin, size());
  std::vector<std::int16_t> samples(static_cast<std::size_t>(end - begin));
  file_->Read(begin, samples);
  std::vector<float> out(samples.size());
  std::transform(
      samples.begin(), samples.end(), out.begin(),
      [](std::int16_t s) { return static_cast<float>(s) / 32768.0f; });
  return out;
}

std::vector<float> AudioBuffer::FrameEnergy(int frame) const {
  if (frame <= 0) throw std::invalid_argument("frame must be positive");
  std::vector<float> energy;
  energy.reserve(static_cast<std::size_t>((size() + frame - 1) / frame));
  // Read whole frames in large pieces so the file is scanned once.
  const SampleIndex piece = SampleIndex{frame} * 4096;
  std::vector<std::int16_t> samples;
  for (SampleIndex start = 0; start < size(); start += piece) {
    samples.resize(static_cast<std::size_t>(std::min(piece, size() - start)));
    file_->Read(start, samples);
    for (std::size_t first = 0; first < samples.size();
         first += static_cast<std::size_t>(frame)) {
      const std::size_t last =
          std::min(first + static_cast<std::size_t>(frame), samples.size());
      double sum = 0.0;
      for (std::size_t i = first; i < last; ++i) {
        const double value = samples[i] / 32768.0;
        sum += value * value;
      }
      energy.push_back(
          static_cast<float>(sum / static_cast<double>(last - first)));
    }
  }
  return energy;
}

}  // namespace sillok
