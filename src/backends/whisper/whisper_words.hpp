#ifndef SILLOK_BACKENDS_WHISPER_WHISPER_WORDS_HPP_
#define SILLOK_BACKENDS_WHISPER_WHISPER_WORDS_HPP_

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/audio.hpp"
#include "core/transcript.hpp"

namespace sillok {

// Text Whisper decoded with its time span. `text` holds raw bytes: one UTF-8
// character may be split over consecutive tokens.
struct Token {
  std::string text;
  Milliseconds start{0};
  Milliseconds end{0};
};

// Groups decoded tokens into words, with the tokens' times.
// In languages with spaces a word starts at a token that begins with a space;
// otherwise every token starts one. A UTF-8 character split over tokens stays
// in one word, and the result is always well-formed UTF-8. In languages
// without spaces a word has one space before it where Whisper put any (none
// before the first word), as Qwen's words have.
std::vector<Word> AssembleWords(std::span<const Token> tokens,
                                std::string_view language);

// The glossary as one prompt: the terms separated by ", ".
std::string JoinGlossary(std::span<const std::string> glossary);

}  // namespace sillok

#endif  // SILLOK_BACKENDS_WHISPER_WHISPER_WORDS_HPP_
