#ifndef SILLOK_CORE_POSTPROCESS_HPP_
#define SILLOK_CORE_POSTPROCESS_HPP_

#include <vector>

#include "core/audio.hpp"
#include "core/transcript.hpp"

namespace sillok {

struct PostprocessOptions {
  Milliseconds max_segment_duration{10000};
};

// Splits segments into sentences, after every word ending in sentence
// punctuation, and then halves sentences longer than max_segment_duration
// until none is: at the longest pause between words if there is one, else
// between the words nearest the middle. A model may give a sentence in
// pieces or several in one segment, or a whole group of speech as one.
std::vector<Segment> SplitSegments(std::vector<Segment> segments,
                                   const PostprocessOptions& options);

}  // namespace sillok

#endif  // SILLOK_CORE_POSTPROCESS_HPP_
