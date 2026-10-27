#include "core/words.hpp"

#include <algorithm>
#include <array>

#include "core/utf8.hpp"

namespace sillok {

bool UsesSpaces(std::string_view language) {
  constexpr std::array<std::string_view, 8> kWithoutSpaces = {
      "ja", "zh", "yue", "th", "lo", "km", "my", "bo"};
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

std::vector<Word> AssembleWords(std::span<const Token> tokens, Millis offset,
                                std::string_view language) {
  const bool spaces = UsesSpaces(language);
  std::vector<Word> words;
  std::vector<int> token_counts;  // tokens per word, for the confidence mean
  std::string pending;            // start of a character not yet complete

  for (const Token& token : tokens) {
    const bool continues_character = !pending.empty();
    std::string bytes = pending + token.text;
    const std::size_t tail = IncompleteUtf8Tail(bytes);
    pending = bytes.substr(bytes.size() - tail);
    bytes.resize(bytes.size() - tail);

    const bool starts_word =
        words.empty() ||
        (!continues_character && (!spaces || bytes.starts_with(' ')));
    if (starts_word) {
      words.push_back(Word{.text = {},
                           .start = token.start + offset,
                           .end = token.end + offset,
                           .confidence = 0.0f,
                           .speaker = std::nullopt});
      token_counts.push_back(0);
    }
    Word& word = words.back();
    word.text += bytes;
    word.end = token.end + offset;
    word.confidence += token.probability;
    ++token_counts.back();
  }
  if (!pending.empty() && !words.empty()) words.back().text += pending;

  std::vector<Word> result;
  result.reserve(words.size());
  for (std::size_t i = 0; i < words.size(); ++i) {
    Word& word = words[i];
    const auto first = word.text.find_first_not_of(' ');
    if (first == std::string::npos) continue;
    word.text = SanitizeUtf8(std::string_view(word.text).substr(first));
    word.confidence /= static_cast<float>(token_counts[i]);
    result.push_back(std::move(word));
  }
  return result;
}

}  // namespace sillok
