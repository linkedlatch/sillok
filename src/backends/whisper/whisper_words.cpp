#include "backends/whisper/whisper_words.hpp"

#include <optional>
#include <utility>

#include "core/utf8.hpp"
#include "core/words.hpp"

namespace sillok {

std::vector<Word> AssembleWords(std::span<const Token> tokens,
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
                           .start = token.start,
                           .end = token.end,
                           .speaker = std::nullopt});
    }
    Word& word = words.back();
    word.text += bytes;
    word.end = token.end;
  }
  if (!pending.empty() && !words.empty()) words.back().text += pending;

  std::vector<Word> result;
  result.reserve(words.size());
  bool space_before = false;  // a space after the last word kept
  for (Word& word : words) {
    const std::string_view text = word.text;
    const auto first = text.find_first_not_of(' ');
    if (first == std::string::npos) {
      space_before = space_before || !text.empty();
      continue;
    }
    if (uses_spaces) {
      word.text = SanitizeUtf8(text.substr(first));
    } else {
      // As Qwen's words: one space before a word where Whisper put any, none
      // at the start.
      const auto last = text.find_last_not_of(' ');
      std::string kept = (space_before || first > 0) && !result.empty() ? " " : "";
      kept += text.substr(first, last + 1 - first);
      space_before = last + 1 < text.size();
      word.text = SanitizeUtf8(kept);
    }
    result.push_back(std::move(word));
  }
  return result;
}

std::string JoinGlossary(std::span<const std::string> glossary) {
  std::string joined;
  for (const std::string& term : glossary) {
    if (!joined.empty()) joined += ", ";
    joined += term;
  }
  return joined;
}

}  // namespace sillok
