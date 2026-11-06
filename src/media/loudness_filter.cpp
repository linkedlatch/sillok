#include "media/loudness_filter.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
}

#include "core/audio.hpp"

namespace sillok {
namespace {

// The filter's defaults but for b=1, which keeps the gain at the start and end
// of a recording: by default it fades in from none there, which leaves a
// recording shorter than 15 s as quiet as it came.
constexpr const char* kOptions = "b=1";

AVFilterContext* Create(AVFilterGraph* graph, const char* filter, const char* name,
                        const char* options) {
  AVFilterContext* context = nullptr;
  if (avfilter_graph_create_filter(&context, avfilter_get_by_name(filter), name, options,
                                   nullptr, graph) < 0) {
    throw std::runtime_error(std::string("cannot set up the filter ") + filter);
  }
  return context;
}

}  // namespace

void LoudnessFilter::GraphFree::operator()(AVFilterGraph* graph) const {
  avfilter_graph_free(&graph);
}

void LoudnessFilter::FrameFree::operator()(AVFrame* frame) const { av_frame_free(&frame); }

LoudnessFilter::LoudnessFilter(Output output)
    : output_(std::move(output)), graph_(avfilter_graph_alloc()), frame_(av_frame_alloc()) {
  if (!graph_ || !frame_) throw std::bad_alloc();
  const std::string format = "sample_rate=" + std::to_string(kSampleRate) +
                             ":sample_fmt=dblp:channel_layout=mono:time_base=1/" +
                             std::to_string(kSampleRate);
  source_ = Create(graph_.get(), "abuffer", "in", format.c_str());
  AVFilterContext* filter = Create(graph_.get(), "dynaudnorm", "level", kOptions);
  sink_ = Create(graph_.get(), "abuffersink", "out", nullptr);
  if (avfilter_link(source_, 0, filter, 0) < 0 || avfilter_link(filter, 0, sink_, 0) < 0 ||
      avfilter_graph_config(graph_.get(), nullptr) < 0) {
    throw std::runtime_error("cannot set up the loudness filter");
  }
}

LoudnessFilter::~LoudnessFilter() = default;

void LoudnessFilter::Push(std::span<const float> samples) {
  if (samples.empty()) return;
  AVFrame* frame = frame_.get();
  const AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
  frame->format = AV_SAMPLE_FMT_DBLP;
  frame->sample_rate = kSampleRate;
  frame->nb_samples = static_cast<int>(samples.size());
  frame->pts = position_;
  if (av_channel_layout_copy(&frame->ch_layout, &mono) < 0 ||
      av_frame_get_buffer(frame, 0) < 0) {
    throw std::bad_alloc();
  }
  auto* plane = reinterpret_cast<double*>(frame->extended_data[0]);
  std::copy(samples.begin(), samples.end(), plane);
  position_ += static_cast<std::int64_t>(samples.size());
  if (av_buffersrc_add_frame(source_, frame) < 0) {
    throw std::runtime_error("cannot filter the audio");
  }
  Drain();
}

void LoudnessFilter::Finish() {
  if (av_buffersrc_add_frame(source_, nullptr) < 0) {
    throw std::runtime_error("cannot filter the audio");
  }
  Drain();
}

void LoudnessFilter::Drain() {
  AVFrame* frame = frame_.get();
  for (;;) {
    const int received = av_buffersink_get_frame(sink_, frame);
    if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) return;
    if (received < 0) throw std::runtime_error("cannot filter the audio");
    const auto* plane = reinterpret_cast<const double*>(frame->extended_data[0]);
    samples_.resize(static_cast<std::size_t>(frame->nb_samples));
    std::transform(plane, plane + frame->nb_samples, samples_.begin(),
                   [](double x) { return static_cast<float>(x); });
    av_frame_unref(frame);
    output_(samples_);
  }
}

}  // namespace sillok
