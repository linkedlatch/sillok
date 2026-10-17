#include <doctest/doctest.h>

#include <fstream>

#include "models.hpp"
#include "support.hpp"
#include "wav.hpp"

using namespace sillok;
using namespace sillok::testing;

namespace {

std::filesystem::path writeFile(const std::string& name, const std::string& content) {
    const auto path = std::filesystem::temp_directory_path() / ("sillok-models-" + makeUUID()) / name;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << content;
    return path;
}

ModelManifest sharedManifest() {
    std::ifstream input(repoRoot() / "shared" / "manifest.json");
    return nlohmann::json::parse(input).get<ModelManifest>();
}

std::vector<std::string> ids(const std::vector<ModelEntry>& entries) {
    std::vector<std::string> result;
    for (const auto& entry : entries) result.push_back(entry.id);
    return result;
}

}

TEST_CASE("sha256 matches the standard test vectors") {
    CHECK(sha256Hex(writeFile("empty", "")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex(writeFile("abc", "abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex(writeFile("million", std::string(1000000, 'a'))) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("shared manifest selects required models") {
    const auto manifest = sharedManifest();
    CHECK(ids(ModelSelection::required(manifest, ModelSelection::defaultWhisperId, true, false)) ==
          std::vector<std::string>{"large-v3-turbo-q5_0", "large-v3-turbo-encoder", "silero-v6.2.0"});
    CHECK(ids(ModelSelection::required(manifest, ModelSelection::defaultWhisperId, false, true)) ==
          std::vector<std::string>{"large-v3-turbo-q5_0", "silero-v6.2.0", "nemotron-3-diarization-q8_0"});
    CHECK(ModelSelection::required(manifest, "missing", true, true).empty());
    const auto* encoder = manifest.entry("large-v3-turbo-encoder");
    REQUIRE(encoder);
    CHECK(encoder->kind == ModelKind::coreMLEncoder);
    CHECK(encoder->installedName() == "ggml-large-v3-turbo-encoder.mlmodelc");
    CHECK(nlohmann::json(manifest) == nlohmann::json::parse(std::ifstream(repoRoot() / "shared" / "manifest.json")));
}

TEST_CASE("core ml needs room for the models and runtime") {
    const auto manifest = sharedManifest();
    const auto& whisper = *manifest.entry(ModelSelection::defaultWhisperId);
    const auto* encoder = manifest.coreMLEncoder(whisper);
    const int64_t needed = whisper.size + encoder->size + ModelSelection::runtimeOverhead;
    CHECK(ModelSelection::shouldUseCoreML(whisper, encoder, needed, true));
    CHECK_FALSE(ModelSelection::shouldUseCoreML(whisper, encoder, needed - 1, true));
    CHECK(ModelSelection::shouldUseCoreML(whisper, encoder, needed - encoder->size, false));
    CHECK_FALSE(ModelSelection::shouldUseCoreML(whisper, nullptr, INT64_MAX, false));
}

TEST_CASE("install verifies the checksum") {
    ModelEntry entry;
    entry.id = "m";
    entry.kind = ModelKind::vad;
    entry.fileName = "m.bin";
    entry.size = 3;
    entry.sha256 = "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD";
    const auto root = std::filesystem::temp_directory_path() / ("sillok-models-" + makeUUID());
    installVerified(writeFile("good", "abc"), entry, root);
    CHECK(isInstalled(entry, root));

    const auto bad = writeFile("bad", "abd");
    CHECK_THROWS_AS(installVerified(bad, entry, root), ChecksumMismatch);
    CHECK_FALSE(std::filesystem::exists(bad));
}

TEST_CASE("wav reader returns the same samples as the whole file") {
    const auto path = fixturePath("en.wav");
    if (!std::filesystem::exists(path)) return;
    const auto expected = readWav(path);
    WavReader reader(path);
    CHECK(reader.duration() == doctest::Approx(expected.size() / 16000.0));
    const auto slice = reader.read(0.5, 1.0);
    REQUIRE(slice.size() == 8000);
    CHECK(slice[123] == expected[8000 + 123]);
    CHECK(reader.read(reader.duration() - 0.1, reader.duration() + 5).size() == 1600);
    CHECK(reader.read(reader.duration() + 1, reader.duration() + 2).empty());
    CHECK_THROWS_AS(WavReader(repoRoot() / "shared" / "manifest.json"), std::invalid_argument);
}
