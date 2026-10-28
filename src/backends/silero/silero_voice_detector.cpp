#include "backends/silero/silero_voice_detector.hpp"

#include <whisper.h>

#include "core/files.hpp"
#include "core/model_provider.hpp"

namespace sillok {
namespace {

// Samples per probability of Silero at 16 kHz.
constexpr int kFrameSamples = 512;

class SileroVoiceDetector final : public VoiceDetector {
 public:
  explicit SileroVoiceDetector(const SileroConfig& config)
      : context_(nullptr, whisper_vad_free),
        info_{.name = config.name, .device = "CPU", .fell_back_to_cpu = false} {
    whisper_vad_context_params params = whisper_vad_default_context_params();
    params.n_threads = config.threads;
    params.use_gpu = false;
    const std::string path = PathToUtf8(config.model);
    context_.reset(whisper_vad_init_from_file_with_params(path.c_str(), params));
    if (!context_) throw ModelError("cannot load the VAD model " + path);
  }

  ModelInfo Info() const override { return info_; }
  int FrameSamples() const override { return kFrameSamples; }

  std::vector<float> Detect(std::span<const float> pcm) override {
    if (!whisper_vad_detect_speech_no_reset(context_.get(), pcm.data(),
                                            static_cast<int>(pcm.size()))) {
      throw ModelError("whisper_vad_detect_speech failed");
    }
    const int count = whisper_vad_n_probs(context_.get());
    const auto expected = (pcm.size() + kFrameSamples - 1) / kFrameSamples;
    if (count < 0 || static_cast<std::size_t>(count) != expected) {
      throw ModelError("the VAD model does not use 512-sample frames");
    }
    const float* probabilities = whisper_vad_probs(context_.get());
    return {probabilities, probabilities + count};
  }

 private:
  std::unique_ptr<whisper_vad_context, decltype(&whisper_vad_free)> context_;
  ModelInfo info_;
};

}  // namespace

std::unique_ptr<VoiceDetector> OpenSilero(const SileroConfig& config) {
  return std::make_unique<SileroVoiceDetector>(config);
}

}  // namespace sillok
