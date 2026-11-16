#ifndef SILLOK_CORE_ALIGNER_HPP_
#define SILLOK_CORE_ALIGNER_HPP_

#include <span>
#include <string>
#include <vector>

#include "core/audio.hpp"
#include "core/cancel.hpp"
#include "core/transcriber.hpp"
#include "core/transcript.hpp"

namespace sillok {

struct AlignRequest {
  std::span<const float> pcm;  // 16 kHz mono, at most MaxAudio()
  std::string text;            // what a transcriber heard in it (TranscribedWords::text)
  std::string language;        // and as which language
};

// Gives a transcriber's text its word times, for a transcriber that leaves
// them out (ModelProvider::OpenAligner). A model of its own, so the pipeline
// runs it after the transcriber is freed.
class Aligner {
 public:
  virtual ~Aligner() = default;

  virtual ModelInfo Info() const = 0;
  // The longest audio one request may hold.
  virtual Milliseconds MaxAudio() const = 0;
  // The words of `request.text`, in order, with times relative to the start
  // of its audio; speakers are not set. Throws ModelError when the text and
  // the audio do not fit the model at once.
  virtual std::vector<Word> Align(const AlignRequest& request, const FractionCallback& on_progress,
                                  StopToken stop) = 0;
};

}  // namespace sillok

#endif  // SILLOK_CORE_ALIGNER_HPP_
