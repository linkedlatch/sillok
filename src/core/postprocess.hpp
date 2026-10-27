#ifndef SILLOK_CORE_POSTPROCESS_HPP_
#define SILLOK_CORE_POSTPROCESS_HPP_

#include <cstddef>
#include <span>
#include <vector>

#include "core/audio.hpp"
#include "core/transcript.hpp"

namespace sillok {

struct PostprocessOptions {
  // Probability at which a voice-detection frame counts as speech.
  float speech_threshold = 0.5f;
  // Segments with less of their time in speech are hallucinations.
  float min_speech_coverage = 0.2f;
  Millis max_segment_duration{8000};
  std::size_t max_segment_characters = 80;
};

// Fraction of `range` whose voice-detection frames are speech.
float SpeechCoverage(std::span<const float> probabilities, int frame_samples,
                     SampleRange range, float threshold);

// Drops segments decoded from (almost) silent audio.
void RemoveSilenceHallucinations(std::vector<Segment>& segments,
                                 std::span<const float> probabilities,
                                 int frame_samples,
                                 const PostprocessOptions& options);

// Splits segments that are too long to read: at sentence punctuation if
// possible, then at commas, then at the longest pause between words.
std::vector<Segment> SplitLongSegments(std::vector<Segment> segments,
                                       const PostprocessOptions& options);

}  // namespace sillok

#endif  // SILLOK_CORE_POSTPROCESS_HPP_
