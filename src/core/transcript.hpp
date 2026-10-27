#ifndef SILLOK_CORE_TRANSCRIPT_HPP_
#define SILLOK_CORE_TRANSCRIPT_HPP_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/audio.hpp"

namespace sillok {

// Times are positions in the original recording.
struct Word {
  std::string text;
  Millis start{0};
  Millis end{0};
  float confidence = 0.0f;
  std::optional<int> speaker;
};

struct Segment {
  Millis start{0};
  Millis end{0};
  std::string text;
  std::string language;  // ISO 639-1
  std::optional<int> speaker;
  std::vector<Word> words;
};

// Which model ran a stage and on what device.
struct ModelInfo {
  std::string name;
  std::string device;
  // A GPU was requested but the model ran on the CPU.
  bool fell_back_to_cpu = false;
};

struct Transcript {
  Millis duration{0};
  std::vector<Segment> segments;
  // Display names chosen by the app, by speaker id. Export falls back to a
  // numbered label for speakers without a name.
  std::map<int, std::string> speaker_names;
  // By stage: "voice_detection", "transcription", "diarization".
  std::map<std::string, ModelInfo> models;
};

}  // namespace sillok

#endif  // SILLOK_CORE_TRANSCRIPT_HPP_
