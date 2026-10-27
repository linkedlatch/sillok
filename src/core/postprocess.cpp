#include "core/postprocess.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include "core/utf8.hpp"
#include "core/words.hpp"

namespace sillok {
namespace {

// How strongly a word ends a phrase: 2 for sentence ends, 1 for commas.
int BreakStrength(std::string_view word) {
  constexpr std::array<std::string_view, 6> kSentence = {
      ".", "?", "!", "\xE3\x80\x82", "\xEF\xBC\x9F", "\xEF\xBC\x81"};  // 。？！
  constexpr std::array<std::string_view, 6> kClause = {
      ",", ";", ":", "\xE3\x80\x81", "\xEF\xBC\x8C", "\xEF\xBC\x9B"};  // 、，；
  for (const std::string_view mark : kSentence) {
    if (word.ends_with(mark)) return 2;
  }
  for (const std::string_view mark : kClause) {
    if (word.ends_with(mark)) return 1;
  }
  return 0;
}

bool TooLong(const Segment& segment, const PostprocessOptions& options) {
  return segment.end - segment.start > options.max_segment_duration ||
         CountCodePoints(segment.text) > options.max_segment_characters;
}

// Number of words to put in the first part when splitting `words`.
std::size_t ChooseSplit(const std::vector<Word>& words) {
  const std::size_t n = words.size();
  // Only split inside the middle 60% to avoid tiny fragments.
  const std::size_t lo = std::max<std::size_t>(1, n * 2 / 10);
  const std::size_t hi = std::max(lo, std::min(n - 1, n * 8 / 10));
  std::size_t best = (n + 1) / 2;
  int best_strength = -1;
  Millis best_pause{0};
  for (std::size_t k = lo; k <= hi; ++k) {
    const int strength = BreakStrength(words[k - 1].text);
    const Millis pause = words[k].start - words[k - 1].end;
    if (strength > best_strength ||
        (strength == best_strength && pause > best_pause)) {
      best = k;
      best_strength = strength;
      best_pause = pause;
    }
  }
  return best;
}

}  // namespace

float SpeechCoverage(std::span<const float> probabilities, int frame_samples,
                     SampleRange range, float threshold) {
  const auto frames = static_cast<SampleIndex>(probabilities.size());
  const SampleIndex first = std::clamp<SampleIndex>(range.begin / frame_samples, 0, frames);
  const SampleIndex last = std::clamp<SampleIndex>(
      (range.end + frame_samples - 1) / frame_samples, first, frames);
  if (first == last) return 0.0f;
  SampleIndex speech = 0;
  for (SampleIndex i = first; i < last; ++i) {
    if (probabilities[static_cast<std::size_t>(i)] >= threshold) ++speech;
  }
  return static_cast<float>(speech) / static_cast<float>(last - first);
}

void RemoveSilenceHallucinations(std::vector<Segment>& segments,
                                 std::span<const float> probabilities,
                                 int frame_samples,
                                 const PostprocessOptions& options) {
  std::erase_if(segments, [&](const Segment& segment) {
    const SampleRange range{ToSamples(segment.start), ToSamples(segment.end)};
    return SpeechCoverage(probabilities, frame_samples, range,
                          options.speech_threshold) <
           options.min_speech_coverage;
  });
}

std::vector<Segment> SplitLongSegments(std::vector<Segment> segments,
                                       const PostprocessOptions& options) {
  std::vector<Segment> out;
  std::vector<Segment> pending;
  for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
    pending.push_back(std::move(*it));
  }
  // Depth-first so that the output stays in order.
  while (!pending.empty()) {
    Segment segment = std::move(pending.back());
    pending.pop_back();
    if (segment.words.size() < 2 || !TooLong(segment, options)) {
      out.push_back(std::move(segment));
      continue;
    }
    const std::size_t split = ChooseSplit(segment.words);
    std::vector<Word> second(
        segment.words.begin() + static_cast<std::ptrdiff_t>(split),
        segment.words.end());
    segment.words.resize(split);
    pending.push_back(MakeSegment(std::move(second), segment.language));
    pending.push_back(MakeSegment(std::move(segment.words), segment.language));
  }
  return out;
}

}  // namespace sillok
