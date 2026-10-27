#include "core/stitcher.hpp"

#include <algorithm>
#include <string>
#include <string_view>

#include "core/words.hpp"

namespace sillok {
namespace {

Millis Midpoint(const Word& word) {
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
                     Millis overlap_begin, Millis overlap_end, Millis seam) {
  // Words that may have been heard by both windows.
  const auto first_shared = static_cast<std::size_t>(
      std::find_if(earlier.begin(), earlier.end(),
                   [&](const Word& w) { return Midpoint(w) >= overlap_begin; }) -
      earlier.begin());
  const auto shared_end = static_cast<std::size_t>(
      std::find_if(later.begin(), later.end(),
                   [&](const Word& w) { return Midpoint(w) >= overlap_end; }) -
      later.begin());

  std::vector<std::string> a;
  for (std::size_t i = first_shared; i < earlier.size(); ++i) {
    a.push_back(Normalize(earlier[i].text));
  }
  std::vector<std::string> b;
  for (std::size_t j = 0; j < shared_end; ++j) {
    b.push_back(Normalize(later[j].text));
  }
  const auto same = [&](std::size_t i, std::size_t j) {
    return !a[i].empty() && a[i] == b[j];
  };

  // Longest common subsequence of the two renderings of the overlap.
  std::vector<std::vector<int>> lcs(a.size() + 1,
                                    std::vector<int>(b.size() + 1, 0));
  for (std::size_t i = a.size(); i-- > 0;) {
    for (std::size_t j = b.size(); j-- > 0;) {
      lcs[i][j] = same(i, j) ? lcs[i + 1][j + 1] + 1
                             : std::max(lcs[i + 1][j], lcs[i][j + 1]);
    }
  }

  bool matched = false;
  StitchCut best;
  Millis best_distance{0};
  for (std::size_t i = 0, j = 0; i < a.size() && j < b.size();) {
    if (same(i, j) && lcs[i][j] == lcs[i + 1][j + 1] + 1) {
      const Millis distance = abs(Midpoint(earlier[first_shared + i]) - seam) +
                              abs(Midpoint(later[j]) - seam);
      if (!matched || distance < best_distance) {
        matched = true;
        best = {first_shared + i + 1, j + 1};
        best_distance = distance;
      }
      ++i;
      ++j;
    } else if (lcs[i + 1][j] >= lcs[i][j + 1]) {
      ++i;
    } else {
      ++j;
    }
  }
  if (matched) return best;

  const auto before_seam = [&](const Word& w) { return Midpoint(w) < seam; };
  return {static_cast<std::size_t>(
              std::find_if_not(earlier.begin(), earlier.end(), before_seam) -
              earlier.begin()),
          static_cast<std::size_t>(
              std::find_if_not(later.begin(), later.end(), before_seam) -
              later.begin())};
}

void KeepLeadingWords(std::vector<Segment>& segments, std::size_t count) {
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
  segments = std::move(kept);
}

void DropLeadingWords(std::vector<Segment>& segments, std::size_t count) {
  std::vector<Segment> kept;
  for (Segment& segment : segments) {
    if (count >= segment.words.size()) {
      count -= segment.words.size();
      continue;
    }
    if (count > 0) {
      segment.words.erase(segment.words.begin(),
                          segment.words.begin() +
                              static_cast<std::ptrdiff_t>(count));
      segment = MakeSegment(std::move(segment.words), segment.language);
      count = 0;
    }
    kept.push_back(std::move(segment));
  }
  segments = std::move(kept);
}

}  // namespace sillok
