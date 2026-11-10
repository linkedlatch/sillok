#ifndef SILLOK_CORE_TRANSCRIBER_HPP_
#define SILLOK_CORE_TRANSCRIBER_HPP_

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "core/audio.hpp"
#include "core/cancel.hpp"
#include "core/transcript.hpp"

namespace sillok {

// Consecutive words a model transcribed, typically a sentence. Times are
// relative to the start of the request audio; speakers are not set.
struct TranscribedWords {
  std::vector<Word> words;
  std::string language;  // what they were transcribed as, one of ModelProvider::Languages()
};

struct TranscribeRequest {
  std::span<const float> pcm;  // 16 kHz mono, of any length
  // One of ModelProvider::Languages(); empty for the model to tell which of
  // them from the audio.
  std::string language;
  std::span<const std::string> glossary;  // terms the model should expect
};

// Receives each piece of words, in order, during Transcribe().
using WordsCallback = std::function<void(TranscribedWords words)>;
// Receives how much of the request is done, a fraction in [0, 1].
using FractionCallback = std::function<void(double fraction)>;

class Transcriber {
 public:
  virtual ~Transcriber() = default;

  virtual ModelInfo Info() const = 0;
  virtual void Transcribe(const TranscribeRequest& request,
                          const WordsCallback& on_words,
                          const FractionCallback& on_progress,
                          StopToken stop) = 0;
};

}  // namespace sillok

#endif  // SILLOK_CORE_TRANSCRIBER_HPP_
