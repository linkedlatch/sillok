#include "core/postprocess.hpp"

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

#include "core/words.hpp"

namespace sillok {
namespace {

bool EndsSentence(std::string_view word) {
  constexpr std::array<std::string_view, 6> kSentenceEnds = {
      ".", "?", "!", "\xE3\x80\x82", "\xEF\xBC\x9F", "\xEF\xBC\x81"};  // 。？！
  for (const std::string_view mark : kSentenceEnds) {
    if (word.ends_with(mark)) return true;
  }
  return false;
}

// Number of words up to the first sentence end before the last word, or 0
// when the words make one sentence.
std::size_t FirstSentence(const std::vector<Word>& words) {
  for (std::size_t k = 1; k < words.size(); ++k) {
    if (EndsSentence(words[k - 1].text)) return k;
  }
  return 0;
}

// Number of words before the split of a sentence too long: the longest pause
// between words, or without any, the start of a word nearest the middle.
std::size_t ChooseSplit(const std::vector<Word>& words) {
  std::size_t best = 0;
  Milliseconds longest{0};
  for (std::size_t k = 1; k < words.size(); ++k) {
    const Milliseconds pause = words[k].start - words[k - 1].end;
    if (pause > longest) {
      best = k;
      longest = pause;
    }
  }
  if (best != 0) return best;
  const Milliseconds twice_middle = words.front().start + words.back().end;
  Milliseconds nearest{0};
  for (std::size_t k = 1; k < words.size(); ++k) {
    const Milliseconds distance = words[k].start * 2 - twice_middle;
    const Milliseconds away = distance < Milliseconds{0} ? -distance : distance;
    if (best == 0 || away < nearest) {
      best = k;
      nearest = away;
    }
  }
  return best;
}

}  // namespace

std::vector<Segment> SplitSegments(std::vector<Segment> segments,
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
    std::size_t split = FirstSentence(segment.words);
    if (split == 0) {
      if (segment.words.size() < 2 ||
          segment.end - segment.start <= options.max_segment_duration) {
        out.push_back(std::move(segment));
        continue;
      }
      split = ChooseSplit(segment.words);
    }
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
