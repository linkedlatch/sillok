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

struct LanguageScore {
  std::string language;  // ISO 639-1
  float probability = 0.0f;
};

// Decoded text with its time span. `text` holds raw bytes: one UTF-8
// character may be split over consecutive tokens. Times are relative to the
// start of the request audio.
struct Token {
  std::string text;
  Milliseconds start{0};
  Milliseconds end{0};
  float probability = 0.0f;
};

// Consecutive tokens the model grouped together, typically a sentence.
struct TokenSegment {
  std::vector<Token> tokens;
};

struct TranscribeRequest {
  std::span<const float> pcm;  // 16 kHz mono, of any length
  std::string language;        // ISO 639-1
  std::string prompt;          // terms the model should expect, or empty
};

// Receives each decoded segment, in order, during Transcribe().
using TokenSegmentCallback = std::function<void(const TokenSegment&)>;

class Transcriber {
 public:
  virtual ~Transcriber() = default;

  virtual ModelInfo Info() const = 0;
  virtual bool DetectsLanguage() const = 0;
  // Probability of each candidate language being spoken in `pcm`. Only
  // called when the model detects languages.
  virtual std::vector<LanguageScore> DetectLanguage(
      std::span<const float> pcm, std::span<const std::string> candidates,
      StopToken stop) = 0;
  virtual void Transcribe(const TranscribeRequest& request,
                          const TokenSegmentCallback& on_segment,
                          StopToken stop) = 0;
};

}  // namespace sillok

#endif  // SILLOK_CORE_TRANSCRIBER_HPP_
