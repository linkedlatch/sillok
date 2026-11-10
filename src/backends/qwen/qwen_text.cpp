#include "backends/qwen/qwen_text.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <utility>

#include "core/words.hpp"

namespace sillok::qwen {
namespace {

struct CodePointRange {
  char32_t first;
  char32_t last;
};

constexpr CodePointRange kWordChars[] = {
#include "backends/qwen/word_chars.inc"
};

constexpr std::string_view kKoreanDict[] = {
#include "backends/qwen/korean_dict.inc"
};

struct Language {
  std::string_view code;
  std::string_view name;
};

constexpr std::array<Language, 11> kLanguages = {{{"ko", "Korean"},
                                                  {"en", "English"},
                                                  {"ja", "Japanese"},
                                                  {"zh", "Chinese"},
                                                  {"yue", "Cantonese"},
                                                  {"de", "German"},
                                                  {"es", "Spanish"},
                                                  {"fr", "French"},
                                                  {"it", "Italian"},
                                                  {"pt", "Portuguese"},
                                                  {"ru", "Russian"}}};

// Code points of well-formed UTF-8 `text`.
std::u32string Decode(std::string_view text) {
  std::u32string out;
  for (std::size_t i = 0; i < text.size();) {
    const auto lead = static_cast<unsigned char>(text[i]);
    const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
    char32_t code = length == 1 ? lead : lead & (0x7F >> length);
    for (std::size_t k = 1; k < length && i + k < text.size(); ++k) {
      code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
    }
    out.push_back(code);
    i += length;
  }
  return out;
}

void Append(std::string& out, char32_t code) {
  if (code < 0x80) {
    out.push_back(static_cast<char>(code));
  } else if (code < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else if (code < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (code >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  }
}

std::string Encode(std::u32string_view text) {
  std::string out;
  for (const char32_t code : text) Append(out, code);
  return out;
}

// Python's str.isspace().
bool IsSpace(char32_t c) {
  return (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20) || c == 0x85 || c == 0xA0 ||
         c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
         c == 0x202F || c == 0x205F || c == 0x3000;
}

// The package's is_kept_char: ' and the letters and numbers.
bool IsKept(char32_t c) {
  if (c == U'\'') return true;
  const auto it = std::upper_bound(
      std::begin(kWordChars), std::end(kWordChars), c,
      [](char32_t value, const CodePointRange& range) { return value < range.first; });
  return it != std::begin(kWordChars) && c <= std::prev(it)->last;
}

// The package's is_cjk_char.
bool IsCjk(char32_t c) {
  return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) ||
         (c >= 0x20000 && c <= 0x2A6DF) || (c >= 0x2A700 && c <= 0x2B73F) ||
         (c >= 0x2B740 && c <= 0x2B81F) || (c >= 0x2B820 && c <= 0x2CEAF) ||
         (c >= 0xF900 && c <= 0xFAFF);
}

bool InKoreanDict(std::u32string_view word) {
  return std::binary_search(std::begin(kKoreanDict), std::end(kKoreanDict), Encode(word));
}

std::u32string_view Strip(std::u32string_view text) {
  while (!text.empty() && IsSpace(text.front())) text.remove_prefix(1);
  while (!text.empty() && IsSpace(text.back())) text.remove_suffix(1);
  return text;
}

// Spans [begin, end) of what lies between spaces, as str.split() gives it.
std::vector<std::pair<std::size_t, std::size_t>> SplitAtSpaces(std::u32string_view text) {
  std::vector<std::pair<std::size_t, std::size_t>> spans;
  std::size_t i = 0;
  while (i < text.size()) {
    if (IsSpace(text[i])) {
      ++i;
      continue;
    }
    const std::size_t begin = i;
    while (i < text.size() && !IsSpace(text[i])) ++i;
    spans.emplace_back(begin, i);
  }
  return spans;
}

// A unit of the kept characters of text[begin, end), if any.
void AddCleaned(std::u32string_view text, std::size_t begin, std::size_t end,
                std::vector<AlignUnit>& units) {
  std::string kept;
  for (std::size_t i = begin; i < end; ++i) {
    if (IsKept(text[i])) Append(kept, text[i]);
  }
  if (!kept.empty()) units.push_back({std::move(kept), begin, end});
}

// soynlp's LTokenizer.tokenize with every dictionary word scoring 1 and the
// rest 0: a word longer than two characters splits after its longest
// beginning (of two characters or more) in the dictionary, and stays whole
// without one.
void AddKorean(std::u32string_view text, std::size_t begin, std::size_t end,
               std::vector<AlignUnit>& units) {
  const std::size_t length = end - begin;
  std::size_t split = length;
  for (std::size_t e = length; length > 2 && e >= 2; --e) {
    if (InKoreanDict(text.substr(begin, e))) {
      split = e;
      break;
    }
  }
  AddCleaned(text, begin, begin + split, units);
  if (split < length) AddCleaned(text, begin + split, end, units);
}

// The package's clean_token and split_segment_with_chinese: the kept
// characters, with each CJK ideograph a unit of its own.
void AddSpaced(std::u32string_view text, std::size_t begin, std::size_t end,
               std::vector<AlignUnit>& units) {
  std::string buffer;
  std::size_t first = 0;
  std::size_t last = 0;
  const auto flush = [&] {
    if (buffer.empty()) return;
    units.push_back({std::move(buffer), first, last});
    buffer.clear();
  };
  for (std::size_t i = begin; i < end; ++i) {
    const char32_t c = text[i];
    if (!IsKept(c)) continue;
    if (IsCjk(c)) {
      flush();
      std::string unit;
      Append(unit, c);
      units.push_back({std::move(unit), i, i + 1});
      continue;
    }
    if (buffer.empty()) first = i;
    Append(buffer, c);
    last = i + 1;
  }
  flush();
}

std::u32string FixCharRepeats(std::u32string_view s, std::size_t threshold) {
  std::u32string out;
  std::size_t i = 0;
  while (i < s.size()) {
    std::size_t count = 1;
    while (i + count < s.size() && s[i + count] == s[i]) ++count;
    if (count > threshold) {
      out.push_back(s[i]);
    } else {
      out.append(s.substr(i, count));
    }
    i += count;
  }
  return out;
}

std::u32string FixPatternRepeats(std::u32string_view s, std::size_t threshold) {
  constexpr std::size_t kMaxLength = 20;
  const std::size_t n = s.size();
  const std::size_t min_chars = threshold * 2;
  if (n < min_chars) return std::u32string(s);
  std::u32string out;
  std::size_t i = 0;
  while (i <= n - min_chars) {
    for (std::size_t k = 1; k <= kMaxLength; ++k) {
      if (i + k * threshold > n) break;
      const std::u32string_view pattern = s.substr(i, k);
      bool valid = true;
      for (std::size_t rep = 1; rep < threshold && valid; ++rep) {
        valid = s.substr(i + rep * k, k) == pattern;
      }
      if (!valid) continue;
      std::size_t end = i + threshold * k;
      while (end + k <= n && s.substr(end, k) == pattern) end += k;
      out.append(pattern);
      out.append(FixPatternRepeats(s.substr(end), threshold));
      return out;
    }
    out.push_back(s[i]);
    ++i;
  }
  out.append(s.substr(i));
  return out;
}

std::string Lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

}  // namespace

std::string_view LanguageName(std::string_view code) {
  for (const Language& language : kLanguages) {
    if (language.code == code) return language.name;
  }
  return {};
}

std::string_view LanguageCode(std::string_view name) {
  for (const Language& language : kLanguages) {
    if (language.name == name) return language.code;
  }
  return {};
}

std::string FixRepetitions(std::string_view text, int threshold) {
  const auto limit = static_cast<std::size_t>(std::max(threshold, 1));
  return Encode(FixPatternRepeats(FixCharRepeats(Decode(text), limit), limit));
}

AsrOutput ParseAsrOutput(std::string_view decoded, std::string_view given_language) {
  const std::u32string all = Decode(decoded);
  const std::u32string_view stripped = Strip(all);
  if (stripped.empty()) return {};
  const std::string text = FixRepetitions(Encode(stripped));
  if (!given_language.empty()) return {std::string(given_language), text};

  constexpr std::string_view kTag = "<asr_text>";
  const std::size_t tag = text.find(kTag);
  if (tag == std::string::npos) return {"", Encode(Strip(Decode(text)))};
  const std::string_view meta = std::string_view(text).substr(0, tag);
  const std::string transcript = Encode(Strip(Decode(std::string_view(text).substr(tag + kTag.size()))));
  if (Lower(meta).find("language none") != std::string::npos) return {"", transcript};

  // The first line of the meta part that is not empty, if it names a
  // language.
  std::string language;
  std::size_t begin = 0;
  while (begin <= meta.size()) {
    std::size_t end = meta.find('\n', begin);
    if (end == std::string_view::npos) end = meta.size();
    const std::string line = Encode(Strip(Decode(meta.substr(begin, end - begin))));
    begin = end + 1;
    if (line.empty()) continue;
    constexpr std::string_view kPrefix = "language ";
    if (Lower(line).starts_with(kPrefix)) {
      const std::string value = Encode(Strip(Decode(std::string_view(line).substr(kPrefix.size()))));
      if (!value.empty()) {
        // Capitalized as normalize_language_name does.
        std::string name = Lower(value);
        if (name[0] >= 'a' && name[0] <= 'z') name[0] = static_cast<char>(name[0] - 'a' + 'A');
        language = std::string(LanguageCode(name));
      }
    }
    break;
  }
  return {language, transcript};
}

std::vector<AlignUnit> AlignUnits(std::string_view text, std::string_view language) {
  const std::u32string chars = Decode(text);
  std::vector<AlignUnit> units;
  for (const auto& [begin, end] : SplitAtSpaces(chars)) {
    if (language == "ko") {
      AddKorean(chars, begin, end, units);
    } else {
      AddSpaced(chars, begin, end, units);
    }
  }
  return units;
}

std::vector<int> FixTimestamps(std::span<const int> times) {
  const std::size_t n = times.size();
  if (n == 0) return {};
  std::vector<std::size_t> length(n, 1);
  std::vector<std::ptrdiff_t> parent(n, -1);
  for (std::size_t i = 1; i < n; ++i) {
    for (std::size_t j = 0; j < i; ++j) {
      if (times[j] <= times[i] && length[j] + 1 > length[i]) {
        length[i] = length[j] + 1;
        parent[i] = static_cast<std::ptrdiff_t>(j);
      }
    }
  }
  std::vector<bool> normal(n, false);
  for (auto at = static_cast<std::ptrdiff_t>(
           std::max_element(length.begin(), length.end()) - length.begin());
       at != -1; at = parent[static_cast<std::size_t>(at)]) {
    normal[static_cast<std::size_t>(at)] = true;
  }

  std::vector<double> result(times.begin(), times.end());
  std::size_t i = 0;
  while (i < n) {
    if (normal[i]) {
      ++i;
      continue;
    }
    std::size_t j = i;
    while (j < n && !normal[j]) ++j;
    std::optional<double> left;
    for (std::size_t k = i; k-- > 0;) {
      if (normal[k]) {
        left = result[k];
        break;
      }
    }
    std::optional<double> right;
    for (std::size_t k = j; k < n; ++k) {
      if (normal[k]) {
        right = result[k];
        break;
      }
    }
    const std::size_t count = j - i;
    for (std::size_t k = i; k < j; ++k) {
      if (!left) {
        result[k] = *right;
      } else if (!right) {
        result[k] = *left;
      } else if (count <= 2) {
        // The nearer side (i - 1 is the left neighbour; ties go left).
        result[k] = k + 1 - i <= j - k ? *left : *right;
      } else {
        result[k] = *left + (*right - *left) / static_cast<double>(count + 1) *
                                static_cast<double>(k - i + 1);
      }
    }
    i = j;
  }
  std::vector<int> fixed;
  fixed.reserve(n);
  for (const double value : result) fixed.push_back(static_cast<int>(value));
  return fixed;
}

std::vector<Word> TimedWords(std::string_view text, std::string_view language,
                             std::span<const AlignUnit> units,
                             std::span<const Milliseconds> times) {
  std::vector<Word> words;
  if (units.empty() || times.size() < units.size() * 2) return words;
  const std::u32string chars = Decode(text);
  const auto make = [&](std::string word_text, std::size_t first, std::size_t last) {
    return Word{.text = std::move(word_text),
                .start = times[first * 2],
                .end = times[last * 2 + 1],
                .speaker = std::nullopt};
  };

  if (UsesSpaces(language)) {
    std::string leading;  // text without units before the first word
    std::size_t unit = 0;
    for (const auto& [begin, end] : SplitAtSpaces(chars)) {
      const std::size_t first = unit;
      while (unit < units.size() && units[unit].begin < end) ++unit;
      std::string piece = Encode(std::u32string_view(chars).substr(begin, end - begin));
      if (unit == first) {
        if (words.empty()) {
          leading += (leading.empty() ? "" : " ") + piece;
        } else {
          words.back().text += " " + piece;
        }
        continue;
      }
      if (!leading.empty()) piece = std::exchange(leading, {}) + " " + piece;
      words.push_back(make(std::move(piece), first, unit - 1));
    }
    return words;
  }

  bool space_before = false;
  for (std::size_t i = 0; i < units.size(); ++i) {
    const std::size_t begin = i == 0 ? 0 : units[i].begin;
    const std::size_t end = i + 1 < units.size() ? units[i + 1].begin : chars.size();
    const std::u32string_view piece = std::u32string_view(chars).substr(begin, end - begin);
    const std::u32string_view kept = Strip(piece);
    std::string word_text = space_before && !words.empty() ? " " : "";
    word_text += Encode(kept);
    space_before = !piece.empty() && IsSpace(piece.back());
    words.push_back(make(std::move(word_text), i, i));
  }
  return words;
}

std::size_t AudioTokens(std::size_t samples) {
  const auto floor_half = [](long long v) { return v >= 0 ? v / 2 : -((-v + 1) / 2); };
  const auto frames = static_cast<long long>(samples / 160);
  const long long rest = frames % 100;
  const long long features = floor_half(rest - 1) + 1;
  const long long tokens = floor_half(floor_half(features - 1) + 1 - 1) + 1 + (frames / 100) * 13;
  return static_cast<std::size_t>(std::max(0LL, tokens));
}

}  // namespace sillok::qwen
