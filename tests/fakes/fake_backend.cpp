// Stands in for src/backends/models.cpp so that the C ABI can be tested
// without third-party code or model files.

#include "backends/models.hpp"
#include "fakes/fake_backend.hpp"

namespace sillok {

std::vector<CatalogEntry> ModelCatalog() {
  return {{.role = "transcription",
           .file = "fake-transcriber.bin",
           .url = "https://example.invalid/fake-transcriber.bin",
           .size = 3,
           .sha256 =
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           .zip = false},
          {.role = "transcription_encoder",
           .file = "fake-encoder.zip",
           .url = "https://example.invalid/fake-encoder.zip",
           .size = 3,
           .sha256 =
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           .zip = true}};
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
  return std::make_unique<testing::FakeModels>(testing::FakeBackendOptions());
}

namespace testing {

FakeOptions& FakeBackendOptions() {
  static FakeOptions* const options = new FakeOptions;
  return *options;
}

}  // namespace testing
}  // namespace sillok
