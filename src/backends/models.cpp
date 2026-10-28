// The one place that decides which concrete models sillok uses.

#include "backends/models.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

#include <model_logging.h>
#include <whisper.h>

#include "backends/ggml_devices.hpp"
#include "backends/silero/silero_voice_detector.hpp"
#include "backends/sortformer/sortformer_diarizer.hpp"
#include "backends/whisper/whisper_transcriber.hpp"
#include "core/files.hpp"

namespace sillok {
namespace {

// Official sources, pinned to a repository revision: whisper.cpp's own
// download scripts point at ggerganov/whisper.cpp and ggml-org/whisper-vad,
// and NVIDIA publishes the diarization GGUF in its model repository.
const CatalogEntry kSilero{
    .role = "voice_detection",
    .file = "ggml-silero-v6.2.0.bin",
    .url = "https://huggingface.co/ggml-org/whisper-vad/resolve/"
           "9ffd54a1e1ee413ddf265af9913beaf518d1639b/ggml-silero-v6.2.0.bin",
    .size = 885098,
    .sha256 = "2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987",
    .zip = false};

const CatalogEntry kWhisper{
    .role = "transcription",
    .file = "ggml-large-v3-turbo-q5_0.bin",
    .url = "https://huggingface.co/ggerganov/whisper.cpp/resolve/"
           "5359861c739e955e79d9a303bcbc70fb988958b1/ggml-large-v3-turbo-q5_0.bin",
    .size = 574041195,
    .sha256 = "394221709cd5ad1f40c46e6031ca61bce88931e6e088c188294c6d5a55ffa7e2",
    .zip = false};

// whisper.cpp looks for the encoder next to the model, named after it
// without the quantization suffix.
const CatalogEntry kWhisperCoreMl{
    .role = "transcription_encoder",
    .file = "ggml-large-v3-turbo-encoder.mlmodelc.zip",
    .url = "https://huggingface.co/ggerganov/whisper.cpp/resolve/"
           "5359861c739e955e79d9a303bcbc70fb988958b1/"
           "ggml-large-v3-turbo-encoder.mlmodelc.zip",
    .size = 1173393014,
    .sha256 = "84bedfe895bd7b5de6e8e89a0803dfc5addf8c0c5bc4c937451716bf7cf7988a",
    .zip = true};

const CatalogEntry kSortformer{
    .role = "diarization",
    .file = "Nemotron-3-Diarization.q8_0.gguf",
    .url = "https://huggingface.co/nvidia/Nemotron-3-Diarization/resolve/"
           "f667ed73aee57d40cc39428eb768b4fd87a0a29e/"
           "Nemotron-3-Diarization.q8_0.gguf",
    .size = 107012128,
    .sha256 = "08456d9e22cd9a323c0364d98375f3746d6e68507ebb705cd46438c534c7a3a1",
    .zip = false};

// The library is quiet unless SILLOK_LOG is set; failures reach the app
// through status codes.
void Log(ggml_log_level, const char* text, void*) {
  static const bool verbose = std::getenv("SILLOK_LOG") != nullptr;
  if (verbose && text != nullptr) std::fputs(text, stderr);
}

void ConfigureLogging() {
  static std::once_flag once;
  std::call_once(once, [] {
    // Marks NeMo-Speech.cpp's logging as configured so that loading a model
    // does not replace the callback set next.
    nemo_speech::common::configure_ggml_logging(false);
    whisper_log_set(Log, nullptr);
  });
}

int DefaultThreads() {
  return static_cast<int>(
      std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
}

class Models final : public ModelProvider {
 public:
  explicit Models(const EngineConfig& config)
      : dir_(config.models_dir),
        device_(ChooseDevice(config.device, config.gpu_index)),
        threads_(config.threads > 0 ? config.threads : DefaultThreads()) {}

  std::unique_ptr<VoiceDetector> OpenVoiceDetector() override {
    return OpenSilero({.model = Path(kSilero),
                       .name = "silero-v6.2.0",
                       .threads = std::min(threads_, 4)});
  }

  std::unique_ptr<Transcriber> OpenTranscriber() override {
#ifdef SILLOK_COREML
    const auto encoder = Path(kWhisperCoreMl).replace_extension();
    const bool core_ml = device_.gpu_index >= 0 &&
                         std::filesystem::is_directory(encoder);
#else
    const bool core_ml = false;
#endif
    return OpenWhisper({.model = Path(kWhisper),
                        .name = "whisper-large-v3-turbo-q5_0",
                        .device = device_,
                        .threads = threads_,
                        .core_ml = core_ml});
  }

  std::unique_ptr<Diarizer> OpenDiarizer() override {
    // NeMo-Speech.cpp's high-accuracy preset for Nemotron-3 (V3); it streams
    // too, so recordings of any length work.
    return OpenSortformer({.model = Path(kSortformer),
                           .name = "nemotron-3-diarization-q8_0",
                           .preset = "v3-offline",
                           .device = device_});
  }

 private:
  std::filesystem::path Path(const CatalogEntry& entry) const {
    return dir_ / PathFromUtf8(entry.file);
  }

  std::filesystem::path dir_;
  ComputeDevice device_;
  int threads_;
};

}  // namespace

std::vector<CatalogEntry> ModelCatalog() {
#ifdef SILLOK_COREML
  return {kSilero, kWhisper, kWhisperCoreMl, kSortformer};
#else
  return {kSilero, kWhisper, kSortformer};
#endif
}

std::vector<DeviceInfo> ListDevices() {
  ConfigureLogging();
  return GgmlDevices();
}

std::unique_ptr<ModelProvider> OpenModels(const EngineConfig& config) {
  ConfigureLogging();
  if (!std::filesystem::is_directory(config.models_dir)) {
    throw ModelError("models directory not found: " +
                     PathToUtf8(config.models_dir));
  }
  // Catch missing or partial downloads now rather than in the middle of a
  // run. Archives are optional: without them the model runs without the
  // accelerator they provide.
  for (const CatalogEntry& entry : ModelCatalog()) {
    if (entry.zip) continue;
    const auto path = config.models_dir / PathFromUtf8(entry.file);
    if (CheckFile(path, entry.size, entry.sha256, false) != FileCheck::kOk) {
      throw ModelError(entry.file + " is missing or incomplete in " +
                       PathToUtf8(config.models_dir));
    }
  }
  return std::make_unique<Models>(config);
}

}  // namespace sillok
