// Stands in for src/backends/models.cpp so that the C ABI can be tested
// without third-party code or model files.

#include "backends/models.hpp"

#include <stdexcept>
#include <string>
#include "fakes/fake_backend.hpp"

namespace sillok {

std::vector<CatalogModel> ModelCatalog() {
  const std::string abc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  return {{.id = "fake-transcriber",
           .role = "transcription",
           .family = "fake",
           .quantization = "q5_0",
           .recommended = true,
           .files = {{.file = "fake-transcriber.bin",
                      .url = "https://example.invalid/fake-transcriber.bin",
                      .size = 3,
                      .sha256 = abc,
                      .zip = false},
                     {.file = "fake-encoder.zip",
                      .url = "https://example.invalid/fake-encoder.zip",
                      .size = 3,
                      .sha256 = abc,
                      .zip = true}}},
          {.id = "fake-small",
           .role = "transcription",
           .family = "fake-small",
           .quantization = "q8_0",
           .recommended = false,
           .files = {{.file = "fake-small.bin",
                      .url = "https://example.invalid/fake-small.bin",
                      .size = 3,
                      .sha256 = abc,
                      .zip = false}}}};
}

std::vector<DeviceInfo> ListDevices() {
  return {{.name = "CPU",
           .description = "fake CPU",
           .type = "cpu",
           .memory_total = 0,
           .memory_free = 0}};
}

std::unique_ptr<ModelProvider> OpenModels(const EngineConfig& config) {
  if (!std::filesystem::is_directory(config.models_dir)) {
    throw ModelError("models directory not found");
  }
  if (!config.transcription_model.empty() && config.transcription_model != "fake-transcriber" &&
      config.transcription_model != "fake-small") {
    throw std::invalid_argument("unknown transcription model: " + config.transcription_model);
  }
  return std::make_unique<testing::FakeModels>(testing::FakeBackendOptions());
}

namespace testing {

FakeOptions& FakeBackendOptions() {
  static FakeOptions* const options = new FakeOptions;
  return *options;
}

}  // namespace testing
}  // namespace sillok
