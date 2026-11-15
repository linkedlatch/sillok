#ifndef SILLOK_CORE_SEGMENTER_HPP_
#define SILLOK_CORE_SEGMENTER_HPP_

#include <span>
#include <utility>
#include <vector>

#include "core/audio.hpp"

namespace sillok {

// The defaults are whisper.cpp's (whisper_vad_default_params), but for
// max_chunk.
struct SegmenterOptions {
  float speech_threshold = 0.5f;    // a frame at or above this starts speech
  float silence_threshold = 0.35f;  // a frame below this ends it
  Milliseconds min_speech{250};     // shorter speech is dropped
  Milliseconds min_silence{100};    // shorter pauses are bridged
  Milliseconds padding{30};         // kept before and after speech
  Milliseconds tail{100};           // kept after speech as well
  // The most speech, joined, that the model hears at once, as WhisperX and
  // faster-whisper hand Whisper its 30 s window; less when the model takes
  // less (Transcriber::MaxAudio()).
  Milliseconds max_chunk{30000};
};

// Speech regions from per-frame speech probabilities, with short pauses
// bridged, short blips dropped and padding added.
std::vector<SampleRange> FindSpeech(std::span<const float> probabilities,
                                    int frame_samples, SampleIndex total,
                                    const SegmenterOptions& options);

// `speech` in chunks transcribed together, each at most `max_chunk` joined
// (JoinedSpeech). A region longer than that is cut first, from its start on:
// each time it reaches `max_chunk`, at the least likely speech frame in the
// second half of what it has so far, so every piece but the last is at least
// half of `max_chunk` (as WhisperX's Binarize cuts). Then consecutive regions
// fill a chunk until the next one would not fit, whatever the pauses between
// them (WhisperX's merge_chunks).
std::vector<std::vector<SampleRange>> ChunkSpeech(std::span<const SampleRange> speech,
                                                  std::span<const float> probabilities,
                                                  int frame_samples, Milliseconds max_chunk);

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
