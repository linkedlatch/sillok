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

// Splits segments that are too long to read: at sentence punctuation if
// possible, then at commas, then at the longest pause between words.
std::vector<Segment> SplitLongSegments(std::vector<Segment> segments,
                                       const PostprocessOptions& options);

}  // namespace sillok

#endif  // SILLOK_CORE_POSTPROCESS_HPP_
