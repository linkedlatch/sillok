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
  const SampleIndex before = ToSamples(options.before);
  const SampleIndex after = ToSamples(options.after);
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

std::vector<std::vector<SampleRange>> ChunkSpeech(std::span<const SampleRange> speech,
                                                  std::span<const float> probabilities,
                                                  int frame_samples, Milliseconds max_chunk) {
  const SampleIndex longest = std::max<SampleIndex>(1, ToSamples(max_chunk));
  const SampleIndex frame = std::max(1, frame_samples);
  const auto probability = [&](SampleIndex index) {
    return static_cast<std::size_t>(index) < probabilities.size()
               ? probabilities[static_cast<std::size_t>(index)]
               : 0.0f;
  };

  std::vector<SampleRange> pieces;
  for (const SampleRange& region : speech) {
    SampleIndex begin = region.begin;
    while (region.end - begin > longest) {
      // At the start of the least likely frame that starts in the second
      // half, the latest of equals for the longer piece, or at the limit if
      // no frame starts there.
      const SampleIndex from = begin + longest / 2;
      const SampleIndex to = begin + longest;
      SampleIndex cut = to;
      float lowest = 2.0f;
      for (SampleIndex index = (from + frame - 1) / frame; index * frame < to; ++index) {
        if (probability(index) <= lowest) {
          lowest = probability(index);
          cut = index * frame;
        }
      }
      pieces.push_back({begin, cut});
      begin = cut;
    }
    pieces.push_back({begin, region.end});
  }

  std::vector<std::vector<SampleRange>> chunks;
  SampleIndex joined = 0;
  for (const SampleRange& piece : pieces) {
    const SampleIndex with = joined + ToSamples(kJoinGap) + piece.length();
    if (chunks.empty() || with > longest) {
      chunks.push_back({piece});
      joined = piece.length();
    } else {
      chunks.back().push_back(piece);
      joined = with;
    }
  }
  return chunks;
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
