#ifndef SILLOK_TESTS_FAKES_FAKE_MODELS_HPP_
#define SILLOK_TESTS_FAKES_FAKE_MODELS_HPP_

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/audio_buffer.hpp"
#include "core/model_provider.hpp"

namespace sillok::testing {

// One spoken word of a synthetic recording.
struct ScriptWord {
  int id = 0;  // 0 <= id < 800, encoded in the burst amplitude
  Millis start{0};
  Millis end{0};
  std::string language = "en";
  int speaker = 0;
};

// Synthetic recording: each word is a square-wave burst whose amplitude
// encodes its id; everything else is silence.
AudioBuffer RenderScript(const std::vector<ScriptWord>& words, Millis duration);

// Text a FakeTranscriber produces for word `id` decoded in `language`.
std::string WordText(int id, const std::string& language);

// Counts live fake models, to check which ones share memory.
struct LiveModels {
  std::atomic<int> detectors = 0;
  std::atomic<int> transcribers = 0;
  std::atomic<int> diarizers = 0;
  // Set when a transcriber and a diarizer were alive at the same time.
  std::atomic<bool> transcriber_with_diarizer = false;
};

struct FakeOptions {
  // Language of each word id, for language detection and decoding.
  std::map<int, std::string> languages;
  // Speaker of each word id, for diarization.
  std::map<int, int> speakers;
  // Called at the start of every Transcribe call with its request.
  std::function<void(const TranscribeRequest&)> on_transcribe;
  bool detects_language = true;
  Millis max_audio{30000};
};

// Provider of fake models that work on RenderScript() audio.
class FakeModels : public ModelProvider {
 public:
  explicit FakeModels(FakeOptions options) : options_(std::move(options)) {}

  std::unique_ptr<VoiceDetector> OpenVoiceDetector() override;
  std::unique_ptr<Transcriber> OpenTranscriber() override;
  std::unique_ptr<Diarizer> OpenDiarizer() override;

  LiveModels& live() { return live_; }
  int transcribe_calls() const { return transcribe_calls_; }

 private:
  FakeOptions options_;
  LiveModels live_;
  int transcribe_calls_ = 0;

  friend class FakeTranscriber;
};

}  // namespace sillok::testing

#endif  // SILLOK_TESTS_FAKES_FAKE_MODELS_HPP_
