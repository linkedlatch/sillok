#ifndef SILLOK_BACKENDS_QWEN_QWEN_TEXT_HPP_
#define SILLOK_BACKENDS_QWEN_QWEN_TEXT_HPP_

// Text handling around Qwen3-ASR and Qwen3-ForcedAligner, as the official
// qwen-asr package (0.0.6) does it, apart from the models so that it is
// testable without them.

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/audio.hpp"
#include "core/transcript.hpp"

namespace sillok::qwen {

// Qwen3-ASR's name of a language ("Korean") from its code ("ko"), and back;
// empty for a language not among sillok's.
std::string_view LanguageName(std::string_view code);
std::string_view LanguageCode(std::string_view name);

// What Qwen3-ASR said: its language (a code, empty when it heard none or one
// not among sillok's) and the transcript.
struct AsrOutput {
  std::string language;
  std::string text;
};

// Parses the decoded text: "language Korean<asr_text>..." when the model
// told the language itself, the transcript alone when the prompt gave it
// (`given_language`, a code). Long repetitions are cut as in the package.
AsrOutput ParseAsrOutput(std::string_view decoded, std::string_view given_language);

// The package's detect_and_fix_repetitions: a character repeated more than
// `threshold` times stays once, and so does a pattern of up to 20 characters
// repeated `threshold` times or more.
std::string FixRepetitions(std::string_view text, int threshold = 20);

// One unit of text Qwen3-ForcedAligner gives a start and an end: letters and
// numbers (and ') of the transcript, at code points [begin, end) of it.
struct AlignUnit {
  std::string text;
  std::size_t begin = 0;
  std::size_t end = 0;
};

// The aligner's units of `text` in `language` (a code). Korean words are
// split as soynlp's LTokenizer does with the package's dictionary; everything
// else at spaces, with each CJK ideograph a unit of its own. Japanese takes
// this second way too, where the package uses the nagisa tagger.
std::vector<AlignUnit> AlignUnits(std::string_view text, std::string_view language);

// The package's fix_timestamp: times (two per unit) off the longest
// non-decreasing run are replaced from their neighbours.
std::vector<int> FixTimestamps(std::span<const int> times);

// Words of `text` timed by its units, with the text's punctuation kept. In
// languages with spaces a word is what lies between spaces and spans its
// units; otherwise each unit is a word, with what follows it up to the next
// one, and one space before it where the text had any. Text without units
// joins the word before it (the one after, at the start). `times` holds a
// start and an end per unit.
std::vector<Word> TimedWords(std::string_view text, std::string_view language,
                             std::span<const AlignUnit> units,
                             std::span<const Milliseconds> times);

// Audio tokens Qwen3-ASR's encoder gives `samples` of 16 kHz audio
// (_get_feat_extract_output_lengths): 13 per second, and fewer for the rest.
// mtmd pads the last 8 s window to whole seconds and gives more.
std::size_t AudioTokens(std::size_t samples);

}  // namespace sillok::qwen

#endif  // SILLOK_BACKENDS_QWEN_QWEN_TEXT_HPP_
