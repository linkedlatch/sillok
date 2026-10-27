#include "core/segmenter.hpp"

#include <algorithm>
#include <stdexcept>

namespace sillok {
namespace {

// Sample at the center of the quietest stretch (about 200 ms) of
// [from, to]. `prefix` holds running sums of per-frame energy.
SampleIndex QuietestPoint(std::span<const double> prefix, int energy_frame,
                          SampleIndex from, SampleIndex to) {
  const auto frames = static_cast<SampleIndex>(prefix.size()) - 1;
  const SampleIndex first =
      std::clamp<SampleIndex>(from / energy_frame, 0, frames);
  const SampleIndex last = std::clamp<SampleIndex>(to / energy_frame, 0, frames);
  if (first >= last) return (from + to) / 2;

  const SampleIndex radius =
      std::max<SampleIndex>(1, ToSamples(Millis{100}) / energy_frame);
  SampleIndex best = first;
  double best_mean = 0.0;
  for (SampleIndex center = first; center < last; ++center) {
    const SampleIndex lo = std::max<SampleIndex>(0, center - radius);
    const SampleIndex hi = std::min<SampleIndex>(frames, center + radius + 1);
    const double mean = (prefix[static_cast<std::size_t>(hi)] -
                         prefix[static_cast<std::size_t>(lo)]) /
                        static_cast<double>(hi - lo);
    if (center == first || mean < best_mean) {
      best = center;
      best_mean = mean;
    }
  }
  return best * energy_frame + energy_frame / 2;
}

}  // namespace

std::vector<SampleRange> FindSpeech(std::span<const float> probabilities,
                                    int frame_samples, SampleIndex total,
                                    const SegmenterOptions& options) {
  const auto to_range = [&](std::size_t first, std::size_t last) {
    return SampleRange{
        static_cast<SampleIndex>(first) * frame_samples,
        std::min<SampleIndex>(static_cast<SampleIndex>(last) * frame_samples,
                              total)};
  };

  std::vector<SampleRange> raw;
  bool in_speech = false;
  std::size_t start = 0;
  for (std::size_t i = 0; i < probabilities.size(); ++i) {
    const float p = probabilities[i];
    if (!in_speech && p >= options.speech_threshold) {
      in_speech = true;
      start = i;
    } else if (in_speech && p < options.silence_threshold) {
      in_speech = false;
      raw.push_back(to_range(start, i));
    }
  }
  if (in_speech) raw.push_back(to_range(start, probabilities.size()));

  std::vector<SampleRange> bridged;
  for (const SampleRange& range : raw) {
    if (!bridged.empty() &&
        range.begin - bridged.back().end < ToSamples(options.min_silence)) {
      bridged.back().end = range.end;
    } else {
      bridged.push_back(range);
    }
  }

  std::vector<SampleRange> speech;
  const SampleIndex padding = ToSamples(options.padding);
  for (const SampleRange& range : bridged) {
    if (range.length() < ToSamples(options.min_speech)) continue;
    const SampleRange padded{std::max<SampleIndex>(0, range.begin - padding),
                             std::min(total, range.end + padding)};
    if (!speech.empty() && padded.begin <= speech.back().end) {
      speech.back().end = padded.end;
    } else {
      speech.push_back(padded);
    }
  }
  return speech;
}

std::vector<Window> PlanWindows(std::span<const SampleRange> speech,
                                std::span<const float> energy,
                                int energy_frame, SampleIndex total,
                                const SegmenterOptions& options) {
  const SampleIndex max_length = ToSamples(options.max_window);
  const SampleIndex join_gap = ToSamples(options.max_join_gap);
  const SampleIndex half_overlap = ToSamples(options.overlap) / 2;
  const SampleIndex search = ToSamples(options.split_search);
  if (half_overlap <= 0 || search <= half_overlap ||
      max_length <= search + 2 * half_overlap) {
    throw std::invalid_argument("inconsistent segmenter options");
  }

  std::vector<double> prefix(energy.size() + 1, 0.0);
  for (std::size_t i = 0; i < energy.size(); ++i) {
    prefix[i + 1] = prefix[i] + static_cast<double>(energy[i]);
  }

  std::vector<Window> windows;
  for (const SampleRange& region : speech) {
    if (!windows.empty()) {
      Window& last = windows.back();
      if (region.begin - last.audio.end <= join_gap &&
          region.end - last.audio.begin <= max_length) {
        last.audio.end = region.end;
        continue;
      }
    }
    SampleIndex begin = region.begin;
    bool overlaps = false;
    while (region.end - begin > max_length) {
      const SampleIndex split =
          QuietestPoint(prefix, energy_frame, begin + max_length - search,
                        begin + max_length - half_overlap);
      windows.push_back(Window{.audio = {begin, split + half_overlap},
                               .owned = {},
                               .overlaps_previous = overlaps});
      begin = split - half_overlap;
      overlaps = true;
    }
    windows.push_back(Window{.audio = {begin, region.end},
                             .owned = {},
                             .overlaps_previous = overlaps});
  }

  for (std::size_t k = 0; k < windows.size(); ++k) {
    windows[k].owned.begin = k == 0 ? 0 : windows[k - 1].owned.end;
    if (k + 1 == windows.size()) {
      windows[k].owned.end = std::max(total, windows[k].audio.end);
    } else if (windows[k + 1].overlaps_previous) {
      windows[k].owned.end = windows[k + 1].audio.begin + half_overlap;
    } else {
      windows[k].owned.end =
          (windows[k].audio.end + windows[k + 1].audio.begin) / 2;
    }
  }
  return windows;
}

}  // namespace sillok
