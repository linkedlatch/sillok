#ifndef SILLOK_BACKENDS_SILERO_SILERO_VOICE_DETECTOR_HPP_
#define SILLOK_BACKENDS_SILERO_SILERO_VOICE_DETECTOR_HPP_

#include <filesystem>
#include <memory>
#include <string>

#include "core/voice_detector.hpp"

namespace sillok {

struct SileroConfig {
  std::filesystem::path model;  // ggml Silero model file
  std::string name;             // for ModelInfo
};

// Silero voice activity detection through whisper.cpp's VAD API. Runs on one
// CPU thread: the model is tiny, and each 32 ms frame is so little work that
// more threads only wait for each other (10 minutes took 1.4 s on one thread,
// 7.5 s on four and 29 s on twelve, with the same probabilities) and a GPU
// would only add start-up cost. Throws ModelError if the model cannot be
// loaded.
std::unique_ptr<VoiceDetector> OpenSilero(const SileroConfig& config);

}  // namespace sillok

#endif  // SILLOK_BACKENDS_SILERO_SILERO_VOICE_DETECTOR_HPP_
