#include "core/postprocess.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string_view>
#include <utility>

#include "core/words.hpp"

namespace sillok {
namespace {

struct CodePointRange {
  char32_t first;
  char32_t last;
};

constexpr CodePointRange kSentenceEnds[] = {
#include "core/sentence_ends.inc"
};

// Last code point of well-formed UTF-8 `text`, or 0 when it is empty.
char32_t LastCodePoint(std::string_view text) {
  std::size_t start = text.size();
  while (start > 0 && (static_cast<unsigned char>(text[start - 1]) & 0xC0) == 0x80) --start;
  if (start == 0) return 0;
  --start;
  const auto lead = static_cast<unsigned char>(text[start]);
  const std::size_t length = text.size() - start;
  char32_t code = length == 1 ? lead : lead & (0x7F >> length);
  for (std::size_t i = start + 1; i < text.size(); ++i) {
    code = (code << 6) | (static_cast<unsigned char>(text[i]) & 0x3F);
  }
  return code;
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

bool EndsSentence(std::string_view word) {
  const char32_t last = LastCodePoint(word);
  const auto it = std::upper_bound(
      std::begin(kSentenceEnds), std::end(kSentenceEnds), last,
      [](char32_t value, const CodePointRange& range) { return value < range.first; });
  return it != std::begin(kSentenceEnds) && last <= std::prev(it)->last;
}

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
