#ifndef SILLOK_CORE_MODEL_PROVIDER_HPP_
#define SILLOK_CORE_MODEL_PROVIDER_HPP_

#include <memory>
#include <stdexcept>

#include "core/diarizer.hpp"
#include "core/transcriber.hpp"
#include "core/voice_detector.hpp"

namespace sillok {

// A model file is missing or corrupt, or the model failed to load or run.
class ModelError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Loads models on demand. The pipeline owns each opened model and decides
// when it is freed, so it controls which models share memory.
class ModelProvider {
 public:
  virtual ~ModelProvider() = default;

  virtual std::unique_ptr<VoiceDetector> OpenVoiceDetector() = 0;
  virtual std::unique_ptr<Transcriber> OpenTranscriber() = 0;
  virtual std::unique_ptr<Diarizer> OpenDiarizer() = 0;
};

}  // namespace sillok

#endif  // SILLOK_CORE_MODEL_PROVIDER_HPP_
