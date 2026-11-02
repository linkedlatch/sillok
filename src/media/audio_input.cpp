#include "media/audio_input.hpp"

#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include "core/audio.hpp"

namespace sillok {

void AudioInput::SwrFree::operator()(SwrContext* context) const { swr_free(&context); }

AudioInput::AudioInput(AudioBuffer& audio) : audio_(audio) {}

AudioInput::~AudioInput() = default;

void AudioInput::Push(std::span<const float> interleaved, int sample_rate,
                      int channels) {
  if (audio_.finished()) throw std::logic_error("audio already finished");
  if (channels < 1 || channels > 64) {
    throw std::invalid_argument("channel count must be in [1, 64]");
  }
  if (interleaved.size() % static_cast<std::size_t>(channels) != 0) {
    throw std::invalid_argument("sample count is not a multiple of channels");
  }
  if (!swr_) {
    if (sample_rate < 8000 || sample_rate > 192000) {
      throw std::invalid_argument("unsupported sample rate " +
                                  std::to_string(sample_rate));
    }
    SwrContext* context = nullptr;
    const AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
    if (swr_alloc_set_opts2(&context, &mono, AV_SAMPLE_FMT_FLT, kSampleRate, &mono,
                            AV_SAMPLE_FMT_FLT, sample_rate, 0, nullptr) < 0) {
      throw std::bad_alloc();
    }
    swr_.reset(context);
    if (swr_init(context) < 0) throw std::runtime_error("cannot set up resampling");
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
  // An empty push would read as a flush.
  if (!mono_.empty()) Convert(mono_.data(), static_cast<int>(mono_.size()));
}

void AudioInput::Finish() {
  if (audio_.finished()) return;
  if (swr_) Convert(nullptr, 0);
  audio_.Finish();
  swr_.reset();
  mono_ = {};
  resampled_ = {};
}

void AudioInput::Convert(const float* mono, int count) {
  resampled_.resize(static_cast<std::size_t>(swr_get_out_samples(swr_.get(), count)));
  auto* out = reinterpret_cast<std::uint8_t*>(resampled_.data());
  const auto* in = reinterpret_cast<const std::uint8_t*>(mono);
  const int made = swr_convert(swr_.get(), &out, static_cast<int>(resampled_.size()),
                               mono != nullptr ? &in : nullptr, count);
  if (made < 0) throw std::runtime_error("cannot resample the audio");
  resampled_.resize(static_cast<std::size_t>(made));
  audio_.Push(resampled_);
}

}  // namespace sillok
