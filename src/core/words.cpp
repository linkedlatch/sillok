#include "core/words.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>

#include "core/utf8.hpp"

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

std::vector<Word> AssembleWords(std::span<const Token> tokens,
                                Milliseconds offset,
                                std::string_view language) {
  const bool uses_spaces = UsesSpaces(language);
  std::vector<Word> words;
  std::string pending;  // start of a character not yet complete

  for (const Token& token : tokens) {
    const bool continues_character = !pending.empty();
    std::string bytes = pending + token.text;
    const std::size_t tail = IncompleteUtf8Tail(bytes);
    pending = bytes.substr(bytes.size() - tail);
    bytes.resize(bytes.size() - tail);

    const bool starts_word =
        words.empty() ||
        (!continues_character && (!uses_spaces || bytes.starts_with(' ')));
    if (starts_word) {
      words.push_back(Word{.text = {},
                           .start = token.start + offset,
                           .end = token.end + offset,
                           .speaker = std::nullopt});
    }
    Word& word = words.back();
    word.text += bytes;
    word.end = token.end + offset;
  }
  if (!pending.empty() && !words.empty()) words.back().text += pending;

  std::vector<Word> result;
  result.reserve(words.size());
  for (Word& word : words) {
    const auto first = word.text.find_first_not_of(' ');
    if (first == std::string::npos) continue;
    word.text = SanitizeUtf8(std::string_view(word.text).substr(first));
    result.push_back(std::move(word));
  }
  return result;
}

}  // namespace sillok
