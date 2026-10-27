#include "core/audio_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sillok {
namespace {

constexpr SampleIndex kBlockSamples = SampleIndex{1} << 20;  // 2 MiB blocks

std::int16_t ToInt16(float value) {
  const float scaled = std::round(value * 32768.0f);
  return static_cast<std::int16_t>(std::clamp(scaled, -32768.0f, 32767.0f));
}

}  // namespace

void AudioBuffer::Push(std::span<const float> interleaved, int sample_rate,
                       int channels) {
  if (finished_) throw std::logic_error("audio already finished");
  if (channels < 1 || channels > 64) {
    throw std::invalid_argument("channel count must be in [1, 64]");
  }
  if (interleaved.size() % static_cast<std::size_t>(channels) != 0) {
    throw std::invalid_argument("sample count is not a multiple of channels");
  }
  if (!resampler_) {
    resampler_.emplace(sample_rate);  // validates the rate
    sample_rate_ = sample_rate;
    channels_ = channels;
  } else if (sample_rate != sample_rate_ || channels != channels_) {
    throw std::invalid_argument("sample rate or channels changed mid-stream");
  }

  const auto count = static_cast<std::size_t>(channels);
  mono_.resize(interleaved.size() / count);
  for (std::size_t frame = 0; frame < mono_.size(); ++frame) {
    float sum = 0.0f;
    for (std::size_t c = 0; c < count; ++c) {
      sum += interleaved[frame * count + c];
    }
    mono_[frame] = sum / static_cast<float>(channels);
  }
  resampled_.clear();
  resampler_->Process(mono_, resampled_);
  Append(resampled_);
}

void AudioBuffer::Finish() {
  if (finished_) return;
  finished_ = true;
  if (!resampler_) return;
  resampled_.clear();
  resampler_->Finish(resampled_);
  Append(resampled_);
  resampler_.reset();
  mono_ = {};
  resampled_ = {};
}

void AudioBuffer::Append(std::span<const float> mono) {
  for (const float value : mono) {
    if (size_ % kBlockSamples == 0) {
      blocks_.emplace_back().reserve(static_cast<std::size_t>(kBlockSamples));
    }
    blocks_.back().push_back(ToInt16(value));
    ++size_;
  }
}

std::int16_t AudioBuffer::At(SampleIndex index) const {
  return blocks_[static_cast<std::size_t>(index / kBlockSamples)]
                [static_cast<std::size_t>(index % kBlockSamples)];
}

std::vector<float> AudioBuffer::Read(SampleRange range) const {
  const SampleIndex begin = std::clamp<SampleIndex>(range.begin, 0, size_);
  const SampleIndex end = std::clamp<SampleIndex>(range.end, begin, size_);
  std::vector<float> out(static_cast<std::size_t>(end - begin));
  for (SampleIndex i = begin; i < end; ++i) {
    out[static_cast<std::size_t>(i - begin)] =
        static_cast<float>(At(i)) / 32768.0f;
  }
  return out;
}

std::vector<float> AudioBuffer::FrameEnergy(int frame) const {
  if (frame <= 0) throw std::invalid_argument("frame must be positive");
  std::vector<float> energy;
  energy.reserve(static_cast<std::size_t>((size_ + frame - 1) / frame));
  for (SampleIndex begin = 0; begin < size_; begin += frame) {
    const SampleIndex end = std::min<SampleIndex>(begin + frame, size_);
    double sum = 0.0;
    for (SampleIndex i = begin; i < end; ++i) {
      const double value = At(i) / 32768.0;
      sum += value * value;
    }
    energy.push_back(static_cast<float>(sum / static_cast<double>(end - begin)));
  }
  return energy;
}

}  // namespace sillok
