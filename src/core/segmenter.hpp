#ifndef SILLOK_CORE_SEGMENTER_HPP_
#define SILLOK_CORE_SEGMENTER_HPP_

#include <span>
#include <vector>

#include "core/audio.hpp"

namespace sillok {

struct SegmenterOptions {
  float speech_threshold = 0.5f;   // a frame at or above this starts speech
  float silence_threshold = 0.35f;  // a frame below this ends it
  Millis min_speech{250};          // shorter speech is dropped
  Millis min_silence{500};         // shorter pauses are bridged
  Millis padding{300};             // kept before and after speech
  Millis max_window{28000};        // longest audio per transcription
  Millis max_join_gap{2000};       // speech further apart starts a new window
  Millis overlap{2000};            // shared by windows split inside speech
  Millis split_search{8000};       // span before max_window searched for a split
};

// A stretch of the recording transcribed in one request.
struct Window {
  SampleRange audio;  // what the transcriber hears
  // Words whose midpoint falls here belong to this window. Owned ranges tile
  // the whole recording.
  SampleRange owned;
  // Split inside continuous speech: the audio overlaps the previous window
  // and the seam between them is owned.begin.
  bool overlaps_previous = false;
};

// Speech regions from per-frame speech probabilities, with short pauses
// bridged, short blips dropped and padding added.
std::vector<SampleRange> FindSpeech(std::span<const float> probabilities,
                                    int frame_samples, SampleIndex total,
                                    const SegmenterOptions& options);

// Windows covering `speech`. Nearby regions share a window while it stays
// within max_window; longer speech is split at its quietest point (by
// `energy`, one value per `energy_frame` samples) with an overlap.
std::vector<Window> PlanWindows(std::span<const SampleRange> speech,
                                std::span<const float> energy,
                                int energy_frame, SampleIndex total,
                                const SegmenterOptions& options);

}  // namespace sillok

#endif  // SILLOK_CORE_SEGMENTER_HPP_
