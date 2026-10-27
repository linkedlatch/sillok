#ifndef SILLOK_CORE_WORDS_HPP_
#define SILLOK_CORE_WORDS_HPP_

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/transcriber.hpp"
#include "core/transcript.hpp"

namespace sillok {

// Whether `language` (ISO 639-1) separates words with spaces.
bool UsesSpaces(std::string_view language);

// Running text of `words` in `language`.
std::string JoinWords(std::span<const Word> words, std::string_view language);

// Segment of `words` in `language`, with text and times taken from the words.
// `words` must not be empty.
Segment MakeSegment(std::vector<Word> words, std::string language);

// Groups decoded tokens into words and shifts their times by `offset`.
// In languages with spaces a word starts at a token that begins with a space;
// otherwise every token starts one. A UTF-8 character split over tokens stays
// in one word, and the result is always well-formed UTF-8.
std::vector<Word> AssembleWords(std::span<const Token> tokens, Millis offset,
                                std::string_view language);

}  // namespace sillok

#endif  // SILLOK_CORE_WORDS_HPP_
