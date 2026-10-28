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
  int threads = 1;
};

// Silero voice activity detection through whisper.cpp's VAD API. Runs on the
// CPU: the model is tiny and a GPU would only add start-up cost. Throws
// ModelError if the model cannot be loaded.
std::unique_ptr<VoiceDetector> OpenSilero(const SileroConfig& config);

}  // namespace sillok

#endif  // SILLOK_BACKENDS_SILERO_SILERO_VOICE_DETECTOR_HPP_
