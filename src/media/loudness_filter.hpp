#ifndef SILLOK_MEDIA_LOUDNESS_FILTER_HPP_
#define SILLOK_MEDIA_LOUDNESS_FILTER_HPP_

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

struct AVFilterContext;
struct AVFilterGraph;
struct AVFrame;

namespace sillok {

// Evens out a 16 kHz mono recording with FFmpeg's dynaudnorm: a quiet passage
// is raised towards the level of the rest over about 15 s, up to 20 dB, and
// keeps its own dynamics. Voice detection and Whisper miss speech recorded
// near -50 dBFS, and a question from the back of a room is quiet next to the
// lecturer. Output lags the input by about that long and is as long.
class LoudnessFilter {
 public:
  using Output = std::function<void(std::span<const float>)>;

  explicit LoudnessFilter(Output output);
  ~LoudnessFilter();
  LoudnessFilter(const LoudnessFilter&) = delete;
  LoudnessFilter& operator=(const LoudnessFilter&) = delete;

  void Push(std::span<const float> samples);
  // Hands over what the filter still holds. Push is not allowed afterwards.
  void Finish();

 private:
  struct GraphFree {
    void operator()(AVFilterGraph* graph) const;
  };
  struct FrameFree {
    void operator()(AVFrame* frame) const;
  };

  // Hands over what the filter has finished.
  void Drain();

  Output output_;
  std::unique_ptr<AVFilterGraph, GraphFree> graph_;
  std::unique_ptr<AVFrame, FrameFree> frame_;
  AVFilterContext* source_ = nullptr;
  AVFilterContext* sink_ = nullptr;
  std::int64_t position_ = 0;
  std::vector<float> samples_;
};

}  // namespace sillok

#endif  // SILLOK_MEDIA_LOUDNESS_FILTER_HPP_
