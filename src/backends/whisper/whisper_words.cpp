#include "backends/whisper/whisper_words.hpp"

#include <optional>
#include <utility>

#include "core/utf8.hpp"
#include "core/words.hpp"

namespace sillok {

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

std::string JoinGlossary(std::span<const std::string> glossary) {
  std::string joined;
  for (const std::string& term : glossary) {
    if (!joined.empty()) joined += ", ";
    joined += term;
  }
  return joined;
}

}  // namespace sillok
