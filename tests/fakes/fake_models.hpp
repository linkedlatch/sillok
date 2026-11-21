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
  int id = 0;  // 0 <= id < 800, encoded in the burst wavelength
  Milliseconds start{0};
  Milliseconds end{0};
  std::string language = "en";
  int speaker = 0;
};

// Synthetic recording: each word is a square-wave burst whose wavelength
// encodes its id; everything else is silence.
AudioBuffer RenderScript(const std::vector<ScriptWord>& words,
                         Milliseconds duration);

// Text a FakeTranscriber produces for word `id` decoded in `language`.
std::string WordText(int id, const std::string& language);

// Counts live fake models, to check which ones share memory.
struct LiveModels {
  std::atomic<int> detectors = 0;
  std::atomic<int> transcribers = 0;
  std::atomic<int> aligners = 0;
  std::atomic<int> diarizers = 0;
  // Set when an aligner was opened while a transcriber was alive.
  std::atomic<bool> aligner_with_transcriber = false;
  // Set when a transcriber and a diarizer were alive at the same time.
  std::atomic<bool> transcriber_with_diarizer = false;
};

struct FakeOptions {
  // Language of each word id, for language detection and decoding.
  std::map<int, std::string> languages;
  // Speaker of each word id, for diarization.
  std::map<int, int> speakers;
  // The longest request the transcriber takes; a longer one fails the test.
  Milliseconds max_audio{30000};
  // The longest glossary the transcriber takes, in bytes of its terms joined
  // by ", " (a token each).
  std::size_t glossary_room = 1000;
  // Called at the start of every Transcribe call with its request.
  std::function<void(const TranscribeRequest&)> on_transcribe;
  // The transcriber hands over each request's text without word times, and
  // an aligner times its words, as Qwen does.
  bool separate_alignment = false;
};

// Provider of fake models that work on RenderScript() audio.
class FakeModels : public ModelProvider {
 public:
  explicit FakeModels(FakeOptions options) : options_(std::move(options)) {}

  std::vector<std::string> Languages() const override { return {"en", "ko"}; }

  std::unique_ptr<VoiceDetector> OpenVoiceDetector() override;
  std::unique_ptr<Transcriber> OpenTranscriber() override;
  std::unique_ptr<Aligner> OpenAligner() override;
  std::unique_ptr<Diarizer> OpenDiarizer() override;

  LiveModels& live() { return live_; }
  int transcribe_calls() const { return transcribe_calls_; }
  int transcribers_opened() const { return transcribers_opened_; }
  int aligners_opened() const { return aligners_opened_; }
  // The longest audio of each CheckGlossary call.
  const std::vector<Milliseconds>& glossary_checks() const { return glossary_checks_; }

 private:
  FakeOptions options_;
  LiveModels live_;
  int transcribe_calls_ = 0;
  int transcribers_opened_ = 0;
  int aligners_opened_ = 0;
  std::vector<Milliseconds> glossary_checks_;

  friend class FakeTranscriber;
};

}  // namespace sillok::testing

#endif  // SILLOK_TESTS_FAKES_FAKE_MODELS_HPP_
