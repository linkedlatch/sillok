#ifndef SILLOK_CORE_TRANSCRIBER_HPP_
#define SILLOK_CORE_TRANSCRIBER_HPP_

#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "core/audio.hpp"
#include "core/transcript.hpp"

namespace sillok {

struct LanguageScore {
  std::string language;  // ISO 639-1
  float probability = 0.0f;
};

// What a transcription model supports; the pipeline adapts to these instead
// of to model types.
struct TranscriberCapabilities {
  Millis max_audio{0};            // longest audio per request
  bool detects_language = false;  // DetectLanguage() is available
  int max_prompt_tokens = 0;      // 0 when prompts are not supported
};

// Decoded text with its time span. `text` holds raw bytes: one UTF-8
// character may be split over consecutive tokens. Times are relative to the
// start of the request audio.
struct Token {
  std::string text;
  Millis start{0};
  Millis end{0};
  float probability = 0.0f;
};

// Consecutive tokens the model grouped together, typically a sentence.
struct TokenSegment {
  std::vector<Token> tokens;
};

struct TranscribeRequest {
  std::span<const float> pcm;  // 16 kHz mono, at most max_audio long
  std::string language;        // ISO 639-1
  std::string prompt;          // within max_prompt_tokens
};

class Transcriber {
 public:
  virtual ~Transcriber() = default;

  virtual ModelInfo Info() const = 0;
  virtual TranscriberCapabilities Capabilities() const = 0;
  // Probability of each candidate language being spoken in `pcm`. Only
  // called when the model detects languages.
  virtual std::vector<LanguageScore> DetectLanguage(
      std::span<const float> pcm, std::span<const std::string> candidates,
      std::stop_token stop) = 0;
  // Length of `text` in prompt tokens.
  virtual int CountTokens(std::string_view text) const = 0;
  virtual std::vector<TokenSegment> Transcribe(
      const TranscribeRequest& request, std::stop_token stop) = 0;
};

}  // namespace sillok

#endif  // SILLOK_CORE_TRANSCRIBER_HPP_
