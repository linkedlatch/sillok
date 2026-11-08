#include "media/audio_file.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>
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

#include "core/audio.hpp"
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
// An output file: closes it, and removes it unless Keep() was called.
class OutputFile {
 public:
  OutputFile(AVFormatContext* context, std::filesystem::path path)
      : context_(context), path_(std::move(path)) {}
  OutputFile(const OutputFile&) = delete;
  OutputFile& operator=(const OutputFile&) = delete;
  ~OutputFile() {
    if (context_->pb != nullptr) avio_closep(&context_->pb);
    avformat_free_context(context_);
    if (!kept_) {
      std::error_code ignored;
      std::filesystem::remove(path_, ignored);
    }
  }
  AVFormatContext* get() const { return context_; }
  void Keep() { kept_ = true; }

 private:
  AVFormatContext* context_;
  std::filesystem::path path_;
  bool kept_ = false;
};

// Enough for the copy to transcribe as the original does (DESIGN D20-5): at
// 32 kbit/s a lecture's character errors rose from 16.1% to 18.5%, at 64 they
// were 16.2%.
constexpr int kCopyBitRate = 64000;

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

void DecodeAudioFile(const std::filesystem::path& path, AudioInput& input,
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
      input.Push(samples, frame->sample_rate, frame->ch_layout.nb_channels);
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

void WriteAacFile(const AudioBuffer& audio, const std::filesystem::path& path,
                  const std::function<void(double)>& progress,
                  const StopToken& stop) {
  ConfigureLogging();
  const std::string name = PathToUtf8(path);
  AVFormatContext* allocated = nullptr;
  if (avformat_alloc_output_context2(&allocated, nullptr, "ipod", name.c_str()) < 0) {
    throw std::bad_alloc();
  }
  OutputFile output(allocated, path);
  const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
  if (encoder == nullptr) throw MediaError("no AAC encoder");
  const std::unique_ptr<AVCodecContext, CodecFree> codec(avcodec_alloc_context3(encoder));
  const std::unique_ptr<AVPacket, PacketFree> packet(av_packet_alloc());
  const std::unique_ptr<AVFrame, FrameFree> frame(av_frame_alloc());
  if (!codec || !packet || !frame) throw std::bad_alloc();
  codec->sample_rate = kSampleRate;
  codec->sample_fmt = AV_SAMPLE_FMT_FLTP;
  codec->bit_rate = kCopyBitRate;
  codec->time_base = {1, kSampleRate};
  av_channel_layout_default(&codec->ch_layout, 1);
  if ((output.get()->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
    codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  if (avcodec_open2(codec.get(), encoder, nullptr) < 0) {
    throw MediaError("cannot open the AAC encoder");
  }
  AVStream* stream = avformat_new_stream(output.get(), nullptr);
  if (stream == nullptr) throw std::bad_alloc();
  stream->time_base = codec->time_base;
  if (avcodec_parameters_from_context(stream->codecpar, codec.get()) < 0) {
    throw std::bad_alloc();
  }
  if (const int error = avio_open(&output.get()->pb, name.c_str(), AVIO_FLAG_WRITE);
      error < 0) {
    throw MediaError("cannot write " + name + ": " + ErrorText(error));
  }
  if (const int error = avformat_write_header(output.get(), nullptr); error < 0) {
    throw MediaError("cannot write " + name + ": " + ErrorText(error));
  }

  // Packets the encoder has finished, into the file.
  const auto write = [&] {
    for (;;) {
      const int error = avcodec_receive_packet(codec.get(), packet.get());
      if (error == AVERROR(EAGAIN) || error == AVERROR_EOF) return;
      if (error < 0) throw MediaError("cannot encode " + name + ": " + ErrorText(error));
      av_packet_rescale_ts(packet.get(), codec->time_base, stream->time_base);
      packet->stream_index = stream->index;
      if (const int written = av_interleaved_write_frame(output.get(), packet.get());
          written < 0) {
        throw MediaError("cannot write " + name + ": " + ErrorText(written));
      }
    }
  };

  const SampleIndex total = audio.size();
  const auto frame_size = static_cast<SampleIndex>(codec->frame_size);
  // Read about ten seconds at a time; the recording lives in a file.
  const SampleIndex block = frame_size * (10 * kSampleRate / frame_size + 1);
  double reported = 0.0;
  for (SampleIndex begin = 0; begin < total; begin += block) {
    ThrowIfStopped(stop);
    const std::vector<float> samples = audio.Read({begin, begin + block});
    for (std::size_t offset = 0; offset < samples.size();
         offset += static_cast<std::size_t>(frame_size)) {
      const std::size_t count =
          std::min(samples.size() - offset, static_cast<std::size_t>(frame_size));
      frame->nb_samples = static_cast<int>(count);
      frame->format = codec->sample_fmt;
      frame->sample_rate = codec->sample_rate;
      if (av_channel_layout_copy(&frame->ch_layout, &codec->ch_layout) < 0 ||
          av_frame_get_buffer(frame.get(), 0) < 0) {
        throw std::bad_alloc();
      }
      std::memcpy(frame->data[0], samples.data() + offset, count * sizeof(float));
      frame->pts = begin + static_cast<SampleIndex>(offset);
      if (const int error = avcodec_send_frame(codec.get(), frame.get()); error < 0) {
        throw MediaError("cannot encode " + name + ": " + ErrorText(error));
      }
      av_frame_unref(frame.get());
      write();
    }
    if (progress) {
      const double fraction = static_cast<double>(std::min(begin + block, total)) /
                              static_cast<double>(total);
      if (fraction >= reported + 0.001) {
        reported = fraction;
        progress(fraction);
      }
    }
  }
  avcodec_send_frame(codec.get(), nullptr);  // flush
  write();
  if (const int error = av_write_trailer(output.get()); error < 0) {
    throw MediaError("cannot write " + name + ": " + ErrorText(error));
  }
  output.Keep();
  if (progress) progress(1.0);
}

}  // namespace sillok
