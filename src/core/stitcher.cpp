#include "core/stitcher.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>

#include "core/words.hpp"

namespace sillok {
namespace {

Milliseconds Midpoint(const Word& word) {
  return word.start + (word.end - word.start) / 2;
}

bool IsPunctuation(char32_t c) {
  return (c >= 0x2000 && c <= 0x206F) ||  // general punctuation
         (c >= 0x3000 && c <= 0x303F) ||  // CJK symbols and punctuation
         (c >= 0xFF01 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20) ||
         (c >= 0xFF3B && c <= 0xFF40) || (c >= 0xFF5B && c <= 0xFF65);
}

// Case-folded (ASCII) text without punctuation, so that two windows'
// renderings of the same word compare equal. `text` is well-formed UTF-8.
std::string Normalize(std::string_view text) {
  std::string out;
  std::size_t i = 0;
  while (i < text.size()) {
    const auto lead = static_cast<unsigned char>(text[i]);
    if (lead < 0x80) {
      if (lead >= 'A' && lead <= 'Z') {
        out.push_back(static_cast<char>(lead - 'A' + 'a'));
      } else if ((lead >= 'a' && lead <= 'z') || (lead >= '0' && lead <= '9')) {
        out.push_back(static_cast<char>(lead));
      }
      ++i;
      continue;
    }
    const std::size_t length = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
    char32_t code = lead & (length == 2 ? 0x1Fu : length == 3 ? 0x0Fu : 0x07u);
    for (std::size_t k = 1; k < length && i + k < text.size(); ++k) {
      code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3Fu);
    }
    if (!IsPunctuation(code)) out.append(text.substr(i, length));
    i += length;
  }
  return out;
}

}  // namespace

StitchCut FindStitch(std::span<const Word> earlier, std::span<const Word> later,
                     Milliseconds overlap_begin, Milliseconds overlap_end,
                     Milliseconds seam) {
  // Words that may have been heard by both windows.
  const auto earlier_shared_begin = static_cast<std::size_t>(
      std::find_if(
          earlier.begin(), earlier.end(),
          [&](const Word& word) { return Midpoint(word) >= overlap_begin; }) -
      earlier.begin());
  const auto later_shared_end = static_cast<std::size_t>(
      std::find_if(
          later.begin(), later.end(),
          [&](const Word& word) { return Midpoint(word) >= overlap_end; }) -
      later.begin());

  std::vector<std::string> earlier_text;
  for (std::size_t i = earlier_shared_begin; i < earlier.size(); ++i) {
    earlier_text.push_back(Normalize(earlier[i].text));
  }
  std::vector<std::string> later_text;
  for (std::size_t j = 0; j < later_shared_end; ++j) {
    later_text.push_back(Normalize(later[j].text));
  }
  const auto same = [&](std::size_t i, std::size_t j) {
    return !earlier_text[i].empty() && earlier_text[i] == later_text[j];
  };

  // Longest common subsequence of the two renderings of the overlap.
  std::vector<std::vector<int>> common_length(
      earlier_text.size() + 1, std::vector<int>(later_text.size() + 1, 0));
  for (std::size_t i = earlier_text.size(); i-- > 0;) {
    for (std::size_t j = later_text.size(); j-- > 0;) {
      common_length[i][j] = same(i, j) ? common_length[i + 1][j + 1] + 1
                                       : std::max(common_length[i + 1][j],
                                                  common_length[i][j + 1]);
    }
  }

  bool matched = false;
  StitchCut best;
  Milliseconds best_distance{0};
  for (std::size_t i = 0, j = 0;
       i < earlier_text.size() && j < later_text.size();) {
    if (same(i, j) && common_length[i][j] == common_length[i + 1][j + 1] + 1) {
      const Milliseconds distance =
          std::chrono::abs(Midpoint(earlier[earlier_shared_begin + i]) - seam) +
          std::chrono::abs(Midpoint(later[j]) - seam);
      if (!matched || distance < best_distance) {
        matched = true;
        best = {earlier_shared_begin + i + 1, j + 1};
        best_distance = distance;
      }
      ++i;
      ++j;
    } else if (common_length[i + 1][j] >= common_length[i][j + 1]) {
      ++i;
    } else {
      ++j;
    }
  }
  if (matched) return best;

  const auto before_seam = [&](const Word& word) {
    return Midpoint(word) < seam;
  };
  return {static_cast<std::size_t>(
              std::find_if_not(earlier.begin(), earlier.end(), before_seam) -
              earlier.begin()),
          static_cast<std::size_t>(
              std::find_if_not(later.begin(), later.end(), before_seam) -
              later.begin())};
}

std::vector<Segment> KeepLeadingWords(std::vector<Segment> segments,
                                      std::size_t count) {
  std::vector<Segment> kept;
  for (Segment& segment : segments) {
    if (count == 0) break;
    if (segment.words.size() <= count) {
      count -= segment.words.size();
      kept.push_back(std::move(segment));
      continue;
    }
    segment.words.resize(count);
    kept.push_back(MakeSegment(std::move(segment.words), segment.language));
    count = 0;
  }
  return kept;
}

std::vector<Segment> DropLeadingWords(std::vector<Segment> segments,
                                      std::size_t count) {
  std::vector<Segment> kept;
  for (Segment& segment : segments) {
    if (count >= segment.words.size()) {
      count -= segment.words.size();
      continue;
    }
    if (count > 0) {
      segment.words.erase(
          segment.words.begin(),
          segment.words.begin() + static_cast<std::ptrdiff_t>(count));
      segment = MakeSegment(std::move(segment.words), segment.language);
      count = 0;
    }
    kept.push_back(std::move(segment));
  }
  return kept;
}

}  // namespace sillok
