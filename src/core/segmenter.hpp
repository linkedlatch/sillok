#ifndef SILLOK_CORE_SEGMENTER_HPP_
#define SILLOK_CORE_SEGMENTER_HPP_

#include <span>
#include <utility>
#include <vector>

#include "core/audio.hpp"

namespace sillok {

// The defaults are whisper.cpp's (whisper_vad_default_params), but for
// max_gap.
struct SegmenterOptions {
  float speech_threshold = 0.5f;    // a frame at or above this starts speech
  float silence_threshold = 0.35f;  // a frame below this ends it
  Milliseconds min_speech{250};     // shorter speech is dropped
  Milliseconds min_silence{100};    // shorter pauses are bridged
  Milliseconds padding{30};         // kept before and after speech
  Milliseconds tail{100};           // kept after speech as well
  // Speech further apart than this is transcribed separately, so that what
  // follows a long pause is not heard together with what came before it.
  Milliseconds max_gap{2000};
  // A group longer than this, joined, is split at its longest pause, and
  // speech without a pause in half, until none is. Qwen3-ForcedAligner takes
  // up to 180 s.
  Milliseconds max_group{180000};
};

// Speech regions from per-frame speech probabilities, with short pauses
// bridged, short blips dropped and padding added.
std::vector<SampleRange> FindSpeech(std::span<const float> probabilities,
                                    int frame_samples, SampleIndex total,
                                    const SegmenterOptions& options);

// `speech` in groups transcribed together: a new one starts at each pause of
// at least `max_gap`, and at the longest pause of a group longer than
// `max_group` joined, or in the middle of speech without one.
std::vector<std::vector<SampleRange>> GroupSpeech(
    std::span<const SampleRange> speech, Milliseconds max_gap, Milliseconds max_group);

// Silence between the regions of joined speech, as in whisper.cpp.
inline constexpr Milliseconds kJoinGap{100};

// Regions of a recording joined with kJoinGap between them, as the
// transcriber hears them, and the way back to times in the recording.
class JoinedSpeech {
 public:
  explicit JoinedSpeech(std::span<const SampleRange> regions);

  SampleIndex size() const { return size_; }
  // Where region `index` starts in the joined speech.
  SampleIndex Offset(std::size_t index) const {
    return pieces_[index].joined;
  }

  // Times in the recording of a word at [start, end) of the joined speech. A
  // word starting in a gap moves to the region after it. It ends inside the
  // region it starts in, or the pause there would become part of it.
  std::pair<Milliseconds, Milliseconds> ToRecording(Milliseconds start,
                                                    Milliseconds end) const;

 private:
  struct Piece {
    SampleIndex joined = 0;
    SampleRange recording;
  };

  std::vector<Piece> pieces_;
  SampleIndex size_ = 0;
};

}  // namespace sillok

#endif  // SILLOK_CORE_SEGMENTER_HPP_
