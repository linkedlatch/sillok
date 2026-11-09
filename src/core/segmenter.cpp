#include "core/segmenter.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace sillok {

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
  const SampleIndex before = ToSamples(options.padding);
  const SampleIndex after = before + ToSamples(options.tail);
  for (const SampleRange& range : bridged) {
    if (range.length() < ToSamples(options.min_speech)) continue;
    const SampleRange padded{std::max<SampleIndex>(0, range.begin - before),
                             std::min(total, range.end + after)};
    if (!speech.empty() && padded.begin <= speech.back().end) {
      speech.back().end = padded.end;
    } else {
      speech.push_back(padded);
    }
  }
  return speech;
}

std::vector<std::vector<SampleRange>> GroupSpeech(
    std::span<const SampleRange> speech, Milliseconds max_gap, Milliseconds max_group) {
  std::vector<std::vector<SampleRange>> pending;
  for (const SampleRange& region : speech) {
    if (pending.empty() ||
        region.begin - pending.back().back().end >= ToSamples(max_gap)) {
      pending.emplace_back();
    }
    pending.back().push_back(region);
  }
  std::vector<std::vector<SampleRange>> groups;
  std::reverse(pending.begin(), pending.end());
  // Depth-first so that the groups stay in order.
  while (!pending.empty()) {
    std::vector<SampleRange> group = std::move(pending.back());
    pending.pop_back();
    if (JoinedSpeech(group).size() <= ToSamples(max_group)) {
      groups.push_back(std::move(group));
      continue;
    }
    if (group.size() == 1) {
      // Speech without a pause: halved, wherever that falls.
      const SampleRange whole = group[0];
      const SampleIndex middle = whole.begin + whole.length() / 2;
      pending.push_back({{middle, whole.end}});
      pending.push_back({{whole.begin, middle}});
      continue;
    }
    std::size_t split = 1;
    for (std::size_t i = 2; i < group.size(); ++i) {
      if (group[i].begin - group[i - 1].end > group[split].begin - group[split - 1].end) {
        split = i;
      }
    }
    pending.emplace_back(group.begin() + static_cast<std::ptrdiff_t>(split), group.end());
    group.resize(split);
    pending.push_back(std::move(group));
  }
  return groups;
}

JoinedSpeech::JoinedSpeech(std::span<const SampleRange> regions) {
  for (const SampleRange& region : regions) {
    if (!pieces_.empty()) size_ += ToSamples(kJoinGap);
    pieces_.push_back({.joined = size_, .recording = region});
    size_ += region.length();
  }
}

std::pair<Milliseconds, Milliseconds> JoinedSpeech::ToRecording(
    Milliseconds start, Milliseconds end) const {
  // The region the word starts in: the first that ends after its start.
  const SampleIndex position = ToSamples(start);
  const auto piece = std::find_if(
      pieces_.begin(), std::prev(pieces_.end()), [&](const Piece& p) {
        return position < p.joined + p.recording.length();
      });
  const auto to_recording = [&](SampleIndex joined) {
    return piece->recording.begin +
           std::clamp<SampleIndex>(joined - piece->joined, 0,
                                   piece->recording.length());
  };
  const Milliseconds first = ToMilliseconds(to_recording(position));
  return {first, std::max(first, ToMilliseconds(to_recording(ToSamples(end))))};
}

}  // namespace sillok
