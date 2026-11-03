#ifndef SILLOK_CORE_STITCHER_HPP_
#define SILLOK_CORE_STITCHER_HPP_

#include <cstddef>
#include <span>
#include <vector>

#include "core/audio.hpp"
#include "core/transcript.hpp"

namespace sillok {

// Where to join the words of two windows that share audio.
struct StitchCut {
  std::size_t earlier_keep = 0;  // words kept from the earlier window
  std::size_t later_skip = 0;    // leading words dropped from the later one
};

// Both windows heard [overlap_begin, overlap_end). The cut keeps every word
// spoken there once: where the two windows recognized the same word, it joins
// at the matching pair nearest `seam`; otherwise each word stays with the
// window on its side of the seam (by its midpoint).
StitchCut FindStitch(std::span<const Word> earlier, std::span<const Word> later,
                     Milliseconds overlap_begin, Milliseconds overlap_end,
                     Milliseconds seam);

// Keeps the first `count` words of `segments`, dropping emptied segments.
std::vector<Segment> KeepLeadingWords(std::vector<Segment> segments,
                                      std::size_t count);
// Removes the first `count` words of `segments`, dropping emptied segments.
std::vector<Segment> DropLeadingWords(std::vector<Segment> segments,
                                      std::size_t count);

}  // namespace sillok

#endif  // SILLOK_CORE_STITCHER_HPP_
