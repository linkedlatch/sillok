// The one place that decides which concrete models sillok uses.

#include "backends/models.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include <llama.h>
#include <model_logging.h>
#include <mtmd.h>
#include <whisper.h>

#include "backends/ggml_devices.hpp"
#include "backends/silero/silero_voice_detector.hpp"
#include "backends/sortformer/sortformer_diarizer.hpp"
#include "backends/qwen/qwen_aligner.hpp"
#include "backends/qwen/qwen_transcriber.hpp"
#include "backends/whisper/whisper_transcriber.hpp"
#include "core/files.hpp"

namespace sillok {
namespace {

// Official sources, pinned to a repository revision: whisper.cpp's own
// download script points at ggerganov/whisper.cpp, and NVIDIA publishes the
// diarization GGUF in its model repository.
constexpr char kWhisperRepo[] =
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/"
    "5359861c739e955e79d9a303bcbc70fb988958b1/";

// Whisper's languages, all of them (docs/DESIGN.md D58), in whisper.cpp's
// order. Javanese is "jv" as in ISO 639-1; whisper.cpp calls it "jw".
const std::vector<std::string>& WhisperLanguages() {
  static const std::vector<std::string> languages = {
      "en", "zh", "de", "es", "ru", "ko", "fr", "ja", "pt", "tr", "pl", "ca", "nl", "ar",
      "sv", "it", "id", "hi", "fi", "vi", "he", "uk", "el", "ms", "cs", "ro", "da", "hu",
      "ta", "no", "th", "ur", "hr", "bg", "lt", "la", "mi", "ml", "cy", "sk", "te", "fa",
      "lv", "bn", "sr", "az", "sl", "kn", "et", "mk", "br", "eu", "is", "hy", "ne", "mn",
      "bs", "kk", "sq", "sw", "gl", "mr", "pa", "si", "km", "sn", "yo", "so", "af", "oc",
      "ka", "be", "tg", "sd", "gu", "am", "yi", "lo", "uz", "fo", "ht", "ps", "tk", "nn",
      "mt", "sa", "lb", "my", "bo", "tl", "mg", "as", "tt", "haw", "ln", "ha", "ba", "jv",
      "su", "yue"};
  return languages;
}

// Qwen's: those Qwen3-ForcedAligner gives word times in (docs/DESIGN.md D43).
const std::vector<std::string>& QwenLanguages() {
  static const std::vector<std::string> languages = {
      "ko", "en", "ja", "zh", "yue", "de", "es", "fr", "it", "pt", "ru"};
  return languages;
}

CatalogFile WhisperFile(const char* file, std::uint64_t size, const char* sha256) {
  return {.file = file, .url = std::string(kWhisperRepo) + file, .size = size,
          .sha256 = sha256, .zip = false};
}

// whisper.cpp looks for the Core ML encoder next to the model, named after
// it without the quantization suffix; Apple builds list it with the model.
CatalogFile CoreMlEncoder(const char* file, std::uint64_t size, const char* sha256) {
  CatalogFile encoder = WhisperFile(file, size, sha256);
  encoder.zip = true;
  return encoder;
}

CatalogModel Whisper(const char* family, const char* quantization,
                     CatalogFile model, CatalogFile encoder,
                     bool recommended = false) {
  CatalogModel entry{.id = std::string("whisper-") + family + "-" + quantization,
                     .role = "transcription",
                     .backend = "whisper",
                     .family = family,
                     .quantization = quantization,
                     .recommended = recommended,
                     .files = {std::move(model)},
                     .languages = WhisperLanguages()};
  // Memory: the weights, the caches (beam search keeps seven copies of the
  // decoder's) and the GPU's compute buffers (the CPU's are smaller), in MB
  // as whisper.cpp reports them; large-v3 has 32 decoder layers, turbo 4
  // (docs/DESIGN.md D65).
  const bool turbo = std::string_view(family) == "large-v3-turbo";
  const double weights_mb = static_cast<double>(model.size) / 1e6;
  const double caches_mb = turbo ? 31.46 + 7.86 + 7 * 10.49 : 251.66 + 7.86 + 7 * 83.89;
  const double compute_mb = turbo ? 233.07 : 240.75;
  entry.memory = static_cast<std::uint64_t>((weights_mb + caches_mb + compute_mb) * 1e6);
#ifdef SILLOK_COREML
  entry.files.push_back(std::move(encoder));
#else
  (void)encoder;
#endif
  return entry;
}

// Qwen3-ASR from ggml-org's conversion, and Qwen3-ForcedAligner converted the
// same way (llama.cpp b12233's convert_hf_to_gguf.py, unchanged): the backend
// runs the four files in this order.
constexpr char kQwenAsrRepo[] =
    "https://huggingface.co/ggml-org/Qwen3-ASR-1.7B-GGUF/resolve/"
    "36a678687ba7d07a74ca70ccb0e36902e005fb80/";
constexpr char kQwenAlignerRepo[] =
    "https://huggingface.co/linkedlatch/Qwen3-ForcedAligner-0.6B-GGUF/resolve/"
    "501a5023b8a60d6baf0bb8f98084208dde4ad058/";

struct QwenFile {
  const char* name;
  std::uint64_t size;
  const char* sha256;
};

// One version of Qwen, its four files: the speech model and its audio encoder,
// then the aligner and its. q8_0 compresses the two language models;
// bf16 is the original weights throughout. Both use the same bf16 encoders.
CatalogModel Qwen(const char* quantization, QwenFile asr, QwenFile aligner) {
  const auto file = [](const char* repo, const QwenFile& f) {
    return CatalogFile{.file = f.name, .url = std::string(repo) + f.name, .size = f.size,
                       .sha256 = f.sha256, .zip = false};
  };
  CatalogModel entry{.id = std::string("qwen3-asr-1.7b-") + quantization,
                   .role = "transcription",
                   .backend = "qwen",
                   .family = "qwen3-asr-1.7b",
                   .quantization = quantization,
                   .recommended = false,
                   .files = {file(kQwenAsrRepo, asr),
                             file(kQwenAsrRepo,
                                  {"mmproj-Qwen3-ASR-1.7B-bf16.gguf", 641773984,
                                   "8882e9ddab3186f9aa71b1417c847177913e1466655ac944cf86e9b846735d62"}),
                             file(kQwenAlignerRepo, aligner),
                             file(kQwenAlignerRepo,
                                  {"mmproj-Qwen3-ForcedAligner-0.6B-bf16.gguf", 639672832,
                                   "88638835f2f97fe800df82401e0145b199fb0884d4d2dc654ae4d3a70d484668"})},
                   .languages = QwenLanguages()};
  entry.aligner = true;
  // Memory: the larger stage, as the speech model is freed before the
  // aligner loads; each takes its two files, its KV cache (28 layers, 2048
  // positions, 1024 wide, f16: 224 MiB), llama.cpp's compute buffers on the
  // GPU (the speech model 333, the aligner 620 MiB; the CPU's are smaller)
  // and its audio encoder's 159 MiB, which mtmd takes 8 s of audio at a time
  // whatever the chunk (docs/DESIGN.md D65, D68).
  constexpr std::uint64_t kMiB = 1024 * 1024;
  const std::uint64_t speech = entry.files[0].size + entry.files[1].size + (224 + 333 + 159) * kMiB;
  const std::uint64_t aligning = entry.files[2].size + entry.files[3].size + (224 + 620 + 159) * kMiB;
  entry.memory = std::max(speech, aligning);
  return entry;
}

std::vector<CatalogModel> BuildCatalog() {
  const CatalogFile large_v3_encoder = CoreMlEncoder(
      "ggml-large-v3-encoder.mlmodelc.zip", 1175711232,
      "47837be7594a29429ec08620043390c4d6d467f8bd362df09e9390ace76a55a4");
  const CatalogFile turbo_encoder = CoreMlEncoder(
      "ggml-large-v3-turbo-encoder.mlmodelc.zip", 1173393014,
      "84bedfe895bd7b5de6e8e89a0803dfc5addf8c0c5bc4c937451716bf7cf7988a");
  return {
      {.id = "silero-v6.2.0",
       .role = "voice_detection",
       .backend = "silero",
       .family = "silero",
       .quantization = "f32",
       .recommended = true,
       .files = {{.file = "ggml-silero-v6.2.0.bin",
                  .url = "https://huggingface.co/ggml-org/whisper-vad/resolve/"
                         "9ffd54a1e1ee413ddf265af9913beaf518d1639b/ggml-silero-v6.2.0.bin",
                  .size = 885098,
                  .sha256 = "2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987",
                  .zip = false}},
       .languages = {}},
      // Transcription: turbo, and large-v3, slower (32 decoder layers to
      // turbo's 4) for possibly a little more accuracy, each in the versions
      // the repository has. Only these two, to keep the choice short
      // (docs/DESIGN.md D27): the smaller models, the .en models (recordings
      // are Korean) and medium (about turbo's size, but slower and less
      // accurate) are left out.
      Whisper("large-v3-turbo", "q5_0",
              WhisperFile("ggml-large-v3-turbo-q5_0.bin", 574041195,
                          "394221709cd5ad1f40c46e6031ca61bce88931e6e088c188294c6d5a55ffa7e2"),
              turbo_encoder, true),
      Whisper("large-v3-turbo", "q8_0",
              WhisperFile("ggml-large-v3-turbo-q8_0.bin", 874188075,
                          "317eb69c11673c9de1e1f0d459b253999804ec71ac4c23c17ecf5fbe24e259a1"),
              turbo_encoder),
      Whisper("large-v3-turbo", "f16",
              WhisperFile("ggml-large-v3-turbo.bin", 1624555275,
                          "1fc70f774d38eb169993ac391eea357ef47c88757ef72ee5943879b7e8e2bc69"),
              turbo_encoder),
      Whisper("large-v3", "q5_0",
              WhisperFile("ggml-large-v3-q5_0.bin", 1081140203,
                          "d75795ecff3f83b5faa89d1900604ad8c780abd5739fae406de19f23ecd98ad1"),
              large_v3_encoder),
      // The repository has no q8_0 of large-v3.
      Whisper("large-v3", "f16",
              WhisperFile("ggml-large-v3.bin", 3095033483,
                          "64d182b440b98d5203c4f9bd541544d84c605196c4f7b845dfa11fb23594d1e2"),
              large_v3_encoder),
      Qwen("q8_0",
           {"Qwen3-ASR-1.7B-Q8_0.gguf", 2165034944,
            "58e22d0532d4eacaf034cfac17a6fed159f37c41390c710186783be439d1fc57"},
           {"Qwen3-ForcedAligner-0.6B-Q8_0.gguf", 645025216,
            "c5b48ae7a04b808f7059de128069f0ad40577c724e9f064aa83bbf11bacefd32"}),
      Qwen("bf16",
           {"Qwen3-ASR-1.7B-bf16.gguf", 4069674944,
            "1af18763dfafde2bbf071ef8a0952f7bee66f140c3565e5ccc5afb07dc1f9227"},
           {"Qwen3-ForcedAligner-0.6B-bf16.gguf", 1208683456,
            "40ee9feb7de271965eee27ed4bd33bdbbdb950a9c6c23f3a4c94702202fcffb8"}),
      {.id = "nemotron-3-diarization-q8_0",
       .role = "diarization",
       .backend = "sortformer",
       .family = "nemotron-3-diarization",
       .quantization = "q8_0",
       .recommended = true,
       .files = {{.file = "Nemotron-3-Diarization.q8_0.gguf",
                  .url = "https://huggingface.co/nvidia/Nemotron-3-Diarization/resolve/"
                         "f667ed73aee57d40cc39428eb768b4fd87a0a29e/"
                         "Nemotron-3-Diarization.q8_0.gguf",
                  .size = 107012128,
                  .sha256 = "08456d9e22cd9a323c0364d98375f3746d6e68507ebb705cd46438c534c7a3a1",
                  .zip = false}},
       .languages = {}},
  };
}

const std::vector<CatalogModel>& Catalog() {
  static const std::vector<CatalogModel> catalog = BuildCatalog();
  return catalog;
}

// The model of `role` an engine with `transcription_model` uses (UsedByEngine).
const CatalogModel& FindModel(std::string_view role, std::string_view transcription_model) {
  for (const CatalogModel& model : Catalog()) {
    if (model.role == role && UsedByEngine(model, transcription_model)) return model;
  }
  throw std::invalid_argument("unknown transcription model: " + std::string(transcription_model));
}

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
    // llama.cpp's own log, which NeMo's model loader writes to as well, and
    // its audio encoder's.
    llama_log_set(Log, nullptr);
    mtmd_log_set(Log, nullptr);
  });
}

// Every logical core: on a 6-core, 12-thread Ryzen, 12 threads ran Whisper
// on the CPU 7% faster than 8 and 18% faster than 6 (docs/DESIGN.md D64).
int DefaultThreads() {
  return static_cast<int>(std::max(std::thread::hardware_concurrency(), 1u));
}

class Models final : public ModelProvider {
 public:
  explicit Models(const EngineConfig& config)
      : dir_(config.models_dir),
        device_(ChooseDevice(config.device, config.gpu_index)),
        threads_(config.threads > 0 ? config.threads : DefaultThreads()),
        npu_(config.npu),
        voice_detector_(FindModel("voice_detection", config.transcription_model)),
        transcriber_(FindModel("transcription", config.transcription_model)),
        diarizer_(FindModel("diarization", config.transcription_model)) {}

  std::vector<std::string> Languages() const override { return transcriber_.languages; }

  std::unique_ptr<VoiceDetector> OpenVoiceDetector() override {
    return OpenSilero({.model = Path(voice_detector_), .name = voice_detector_.id});
  }

  std::unique_ptr<Transcriber> OpenTranscriber() override {
    if (transcriber_.backend == "qwen") {
      return OpenQwen({.asr_model = Path(transcriber_, 0),
                       .asr_mmproj = Path(transcriber_, 1),
                       .name = transcriber_.id,
                       .device = device_,
                       .threads = threads_,
                       .languages = transcriber_.languages});
    }
    // The Core ML encoder is the model's archive, once extracted.
    bool core_ml = false;
    for (const CatalogFile& file : transcriber_.files) {
      if (!file.zip) continue;
      const auto encoder = (dir_ / PathFromUtf8(file.file)).replace_extension();
      core_ml = npu_ && std::filesystem::is_directory(encoder);
    }
    return OpenWhisper({.model = Path(transcriber_),
                        .name = transcriber_.id,
                        .device = device_,
                        .threads = threads_,
                        .core_ml = core_ml,
                        .languages = transcriber_.languages});
  }

  // Qwen's word times come from Qwen3-ForcedAligner, its third and fourth
  // files (the one model with an aligner); Whisper's with its words.
  std::unique_ptr<Aligner> OpenAligner() override {
    if (!transcriber_.aligner) return nullptr;
    const std::filesystem::path model = Path(transcriber_, 2);
    return OpenQwenAligner({.model = model,
                            .mmproj = Path(transcriber_, 3),
                            .name = PathToUtf8(model.stem()),
                            .device = device_,
                            .threads = threads_});
  }

  std::unique_ptr<Diarizer> OpenDiarizer() override {
    // NeMo-Speech.cpp's high-accuracy preset for Nemotron-3 (V3); it streams
    // too, so recordings of any length work.
    return OpenSortformer({.model = Path(diarizer_),
                           .name = diarizer_.id,
                           .preset = "v3-offline",
                           .device = device_});
  }

 private:
  // File `index` of the model; the first, the main one, is never an archive.
  std::filesystem::path Path(const CatalogModel& model, std::size_t index = 0) const {
    return dir_ / PathFromUtf8(model.files.at(index).file);
  }

  std::filesystem::path dir_;
  ComputeDevice device_;
  int threads_;
  bool npu_;
  const CatalogModel& voice_detector_;
  const CatalogModel& transcriber_;
  const CatalogModel& diarizer_;
};

}  // namespace

std::vector<CatalogModel> ModelCatalog() { return Catalog(); }

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
  for (const CatalogModel& model : EngineModels(Catalog(), config.transcription_model)) {
    for (const CatalogFile& file : model.files) {
      if (file.zip) continue;
      const auto path = config.models_dir / PathFromUtf8(file.file);
      if (CheckFile(path, file.size, file.sha256, false) != FileCheck::kOk) {
        throw ModelError(file.file + " is missing or incomplete in " +
                         PathToUtf8(config.models_dir));
      }
    }
  }
  return std::make_unique<Models>(config);
}

}  // namespace sillok
