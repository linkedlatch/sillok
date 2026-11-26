#ifndef SILLOK_CAPI_SPEECH_FILE_HPP_
#define SILLOK_CAPI_SPEECH_FILE_HPP_

#include <filesystem>
#include <optional>
#include <string>

#include "core/pipeline.hpp"

namespace sillok {

// A job's file of detected speech (the job option "speech"): a JSON header
// line with the format, the library and the key, then the probabilities as
// little-endian 32-bit floats. A file of another format or library, or cut
// short, is not used. Writing is best effort: when it fails the file is
// removed, and the job carries on without one.
class SpeechFile final : public SpeechStore {
 public:
  // `library` names the library version: a file written by another is not
  // used.
  SpeechFile(std::filesystem::path path, std::string library);

  std::optional<DetectedSpeech> Load() override;
  void Save(const DetectedSpeech& speech) override;

 private:
  std::filesystem::path path_;
  std::string library_;
};

}  // namespace sillok

#endif  // SILLOK_CAPI_SPEECH_FILE_HPP_
