#ifndef SILLOK_CORE_POSTPROCESS_HPP_
#define SILLOK_CORE_POSTPROCESS_HPP_

#include <cstddef>
#include <vector>

#include "core/audio.hpp"
#include "core/transcript.hpp"

namespace sillok {

struct PostprocessOptions {
  Milliseconds max_segment_duration{8000};
  std::size_t max_segment_characters = 80;
};

// Splits segments into sentences, after every word ending in sentence
// punctuation, and then sentences too long to read: at commas if possible,
// then at the longest pause between words. A model may give a sentence in
// pieces or several in one segment, or a whole group of speech as one.
std::vector<Segment> SplitSegments(std::vector<Segment> segments,
                                   const PostprocessOptions& options);

}  // namespace sillok

#endif  // SILLOK_CORE_POSTPROCESS_HPP_
