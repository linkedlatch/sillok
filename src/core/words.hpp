#ifndef SILLOK_CORE_WORDS_HPP_
#define SILLOK_CORE_WORDS_HPP_

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/audio.hpp"
#include "core/transcript.hpp"

namespace sillok {

// Whether `language` (ISO 639-1) separates words with spaces.
bool UsesSpaces(std::string_view language);

// Running text of `words` in `language`.
std::string JoinWords(std::span<const Word> words, std::string_view language);

// Segment of `words` in `language`, with text and times taken from the words.
// `words` must not be empty.
Segment MakeSegment(std::vector<Word> words, std::string language);

// `words` as the rest of the pipeline expects them from a model: without
// empty ones, inside [0, duration] and ending no earlier than they start.
std::vector<Word> CheckWords(std::vector<Word> words, Milliseconds duration);

}  // namespace sillok

#endif  // SILLOK_CORE_WORDS_HPP_
