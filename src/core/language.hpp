#ifndef SILLOK_CORE_LANGUAGE_HPP_
#define SILLOK_CORE_LANGUAGE_HPP_

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/transcriber.hpp"

namespace sillok {

struct LanguageOptions {
  // Another language at or above this probability takes over at once.
  float switch_now = 0.85f;
  // At or above this, it takes over when the next window agrees.
  float switch_confirm = 0.6f;
  // A runner-up at or above this is worth a second decoding when the first
  // one is unconvincing.
  float retry_min = 0.2f;
};

struct LanguageDecision {
  std::string language;
  // Runner-up to decode as well if the result looks wrong.
  std::optional<std::string> alternative;
  // Detection preferred the alternative but the current language was kept;
  // decoding both is the only way not to miss a real switch here.
  bool overruled = false;
};

// Keeps the language of a recording steady across windows while still
// following real switches (hysteresis over per-window detection).
class LanguageStabilizer {
 public:
  LanguageStabilizer(std::vector<std::string> candidates,
                     LanguageOptions options);

  // Language for the next window given its detection scores.
  LanguageDecision Decide(std::span<const LanguageScore> scores);
  // Records the language the window was finally transcribed in.
  void Commit(const std::string& language);

 private:
  std::vector<std::string> candidates_;
  LanguageOptions options_;
  std::optional<std::string> current_;
  std::optional<std::string> pending_;  // challenger seen in the last window
};

}  // namespace sillok

#endif  // SILLOK_CORE_LANGUAGE_HPP_
