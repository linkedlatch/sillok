#ifndef SILLOK_BACKENDS_WHISPER_WHISPER_TRANSCRIBER_HPP_
#define SILLOK_BACKENDS_WHISPER_WHISPER_TRANSCRIBER_HPP_

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "backends/ggml_devices.hpp"
#include "core/transcriber.hpp"

namespace sillok {

struct WhisperConfig {
  std::filesystem::path model;  // ggml model file
  std::string name;             // for ModelInfo
  ComputeDevice device;
  int threads = 4;
  // Whether whisper.cpp may run the encoder through the Core ML model next
  // to `model` (builds with Core ML only), whatever `device` is. Off keeps
  // the Neural Engine unused.
  bool core_ml = false;
  // The languages it may be told or tell from the audio, in Whisper's codes.
  std::vector<std::string> languages;
};

// Whisper speech recognition through whisper.cpp. Throws ModelError if the
// model cannot be loaded or does not know one of the languages.
std::unique_ptr<Transcriber> OpenWhisper(const WhisperConfig& config);

}  // namespace sillok

#endif  // SILLOK_BACKENDS_WHISPER_WHISPER_TRANSCRIBER_HPP_
