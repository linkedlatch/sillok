#include "media/audio_file.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include "core/files.hpp"

namespace sillok {
namespace {

struct FormatClose {
  void operator()(AVFormatContext* context) const { avformat_close_input(&context); }
};
struct CodecFree {
  void operator()(AVCodecContext* context) const { avcodec_free_context(&context); }
};
struct PacketFree {
  void operator()(AVPacket* packet) const { av_packet_free(&packet); }
};
struct FrameFree {
  void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};

// FFmpeg is quiet unless SILLOK_LOG is set, like the model libraries.
void ConfigureLogging() {
  static std::once_flag once;
  std::call_once(once, [] {
    av_log_set_level(std::getenv("SILLOK_LOG") != nullptr ? AV_LOG_INFO : AV_LOG_QUIET);
  });
}

std::string ErrorText(int error) {
  char text[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(error, text, sizeof text);
  return text;
}

template <typename T>
T Load(const std::uint8_t* data, int index) {
  T value;
  std::memcpy(&value, data + static_cast<std::size_t>(index) * sizeof(T), sizeof(T));
  return value;
}

// Sample `index` of a plane of `format` (a packed format) as a float.
float SampleAt(AVSampleFormat format, const std::uint8_t* data, int index) {
  switch (format) {
    case AV_SAMPLE_FMT_U8:
      return static_cast<float>(Load<std::uint8_t>(data, index) - 128) / 128.0f;
    case AV_SAMPLE_FMT_S16:
      return static_cast<float>(Load<std::int16_t>(data, index)) / 32768.0f;
    case AV_SAMPLE_FMT_S32:
      return static_cast<float>(Load<std::int32_t>(data, index) / 2147483648.0);
    case AV_SAMPLE_FMT_S64:
      return static_cast<float>(static_cast<double>(Load<std::int64_t>(data, index)) /
                                9223372036854775808.0);
    case AV_SAMPLE_FMT_FLT:
      return Load<float>(data, index);
    case AV_SAMPLE_FMT_DBL:
      return static_cast<float>(Load<double>(data, index));
    default:
      throw MediaError("unsupported sample format " +
                       std::string(av_get_sample_fmt_name(format)));
  }
}

// The frame's samples as interleaved floats.
void Interleave(const AVFrame& frame, std::vector<float>& out) {
  const int channels = frame.ch_layout.nb_channels;
  const auto format = static_cast<AVSampleFormat>(frame.format);
  const bool planar = av_sample_fmt_is_planar(format) != 0;
  const AVSampleFormat packed = av_get_packed_sample_fmt(format);
  out.resize(static_cast<std::size_t>(frame.nb_samples) * static_cast<std::size_t>(channels));
  std::size_t next = 0;
  for (int i = 0; i < frame.nb_samples; ++i) {
    for (int c = 0; c < channels; ++c) {
      out[next++] = planar ? SampleAt(packed, frame.extended_data[c], i)
                           : SampleAt(packed, frame.extended_data[0], i * channels + c);
    }
  }
}

}  // namespace

void DecodeAudioFile(const std::filesystem::path& path, AudioBuffer& audio,
                     const std::function<void(double)>& progress,
                     const StopToken& stop) {
  ConfigureLogging();
  const std::string name = PathToUtf8(path);
  AVFormatContext* opened = nullptr;
  if (const int error = avformat_open_input(&opened, name.c_str(), nullptr, nullptr);
      error < 0) {
    throw MediaError("cannot open " + name + ": " + ErrorText(error));
  }
  const std::unique_ptr<AVFormatContext, FormatClose> format(opened);
  if (const int error = avformat_find_stream_info(format.get(), nullptr); error < 0) {
    throw MediaError("cannot read " + name + ": " + ErrorText(error));
  }
  const AVCodec* decoder = nullptr;
  const int stream =
      av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
  if (stream < 0) {
    throw MediaError(stream == AVERROR_DECODER_NOT_FOUND
                         ? "no decoder for the audio of " + name
                         : "no audio in " + name);
  }
  // Read past the other streams, such as a lecture's video, without demuxing them.
  for (unsigned i = 0; i < format->nb_streams; ++i) {
    if (static_cast<int>(i) != stream) format->streams[i]->discard = AVDISCARD_ALL;
  }

  const std::unique_ptr<AVCodecContext, CodecFree> codec(avcodec_alloc_context3(decoder));
  const std::unique_ptr<AVPacket, PacketFree> packet(av_packet_alloc());
  const std::unique_ptr<AVFrame, FrameFree> frame(av_frame_alloc());
  if (!codec || !packet || !frame) throw std::bad_alloc();
  if (avcodec_parameters_to_context(codec.get(), format->streams[stream]->codecpar) < 0 ||
      avcodec_open2(codec.get(), decoder, nullptr) < 0) {
    throw MediaError("cannot decode the audio of " + name);
  }

  std::vector<float> samples;
  // Frames the decoder has finished. Damaged frames are skipped, as players do.
  const auto receive = [&] {
    for (;;) {
      const int error = avcodec_receive_frame(codec.get(), frame.get());
      if (error == AVERROR(EAGAIN) || error == AVERROR_EOF) return;
      if (error == AVERROR_INVALIDDATA) continue;
      if (error < 0) throw MediaError("cannot decode " + name + ": " + ErrorText(error));
      Interleave(*frame, samples);
      audio.Push(samples, frame->sample_rate, frame->ch_layout.nb_channels);
      av_frame_unref(frame.get());
    }
  };

  const std::int64_t size = avio_size(format->pb);
  double reported = 0.0;
  for (;;) {
    ThrowIfStopped(stop);
    const int read = av_read_frame(format.get(), packet.get());
    if (read == AVERROR_EOF) break;
    if (read < 0) throw MediaError("cannot read " + name + ": " + ErrorText(read));
    if (packet->stream_index == stream) {
      const int sent = avcodec_send_packet(codec.get(), packet.get());
      av_packet_unref(packet.get());
      if (sent < 0 && sent != AVERROR_INVALIDDATA) {
        throw MediaError("cannot decode " + name + ": " + ErrorText(sent));
      }
      receive();
    } else {
      av_packet_unref(packet.get());
    }
    if (progress && size > 0) {
      const double fraction = std::clamp(
          static_cast<double>(avio_tell(format->pb)) / static_cast<double>(size), 0.0, 1.0);
      if (fraction >= reported + 0.001) {
        reported = fraction;
        progress(fraction);
      }
    }
  }
  avcodec_send_packet(codec.get(), nullptr);  // flush
  receive();
  if (progress) progress(1.0);
}

}  // namespace sillok
