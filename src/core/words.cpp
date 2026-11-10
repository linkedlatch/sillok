#include "core/words.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace sillok {

bool UsesSpaces(std::string_view language) {
  constexpr std::array<std::string_view, 3> kWithoutSpaces = {"ja", "zh", "yue"};
  return std::find(kWithoutSpaces.begin(), kWithoutSpaces.end(), language) ==
         kWithoutSpaces.end();
}

std::string JoinWords(std::span<const Word> words, std::string_view language) {
  const std::string_view separator = UsesSpaces(language) ? " " : "";
  std::string text;
  for (const Word& word : words) {
    if (!text.empty()) text += separator;
    text += word.text;
  }
  return text;
}

Segment MakeSegment(std::vector<Word> words, std::string language) {
  Segment segment;
  segment.start = words.front().start;
  segment.end = words.back().end;
  segment.text = JoinWords(words, language);
  segment.speaker = words.front().speaker;
  segment.language = std::move(language);
  segment.words = std::move(words);
  return segment;
}

std::vector<Word> CheckWords(std::vector<Word> words, Milliseconds duration) {
  std::vector<Word> checked;
  checked.reserve(words.size());
  for (Word& word : words) {
    if (word.text.empty()) continue;
    word.start = std::clamp(word.start, Milliseconds{0}, duration);
    word.end = std::clamp(word.end, word.start, duration);
    checked.push_back(std::move(word));
  }
  return checked;
}

}  // namespace sillok
