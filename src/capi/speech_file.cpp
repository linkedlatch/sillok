#include "capi/speech_file.hpp"

#include <bit>
#include <cstring>
#include <exception>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace sillok {
namespace {

using Json = nlohmann::json;

constexpr int kFormat = 1;

static_assert(std::endian::native == std::endian::little);

}  // namespace

SpeechFile::SpeechFile(std::filesystem::path path, std::string library)
    : path_(std::move(path)), library_(std::move(library)) {}

std::optional<DetectedSpeech> SpeechFile::Load() {
  std::string text;
  {
    std::ifstream in(path_, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream content;
    content << in.rdbuf();
    text = std::move(content).str();
  }
  const std::size_t end = text.find('\n');
  if (end == std::string::npos) return std::nullopt;
  DetectedSpeech speech;
  try {
    const Json header = Json::parse(text.substr(0, end));
    if (header.value("format", 0) != kFormat || header.value("library", "") != library_) {
      return std::nullopt;
    }
    speech.key = header.at("key").get<std::string>();
  } catch (const std::exception&) {
    return std::nullopt;
  }
  const std::size_t bytes = text.size() - end - 1;
  if (bytes % sizeof(float) != 0) return std::nullopt;
  speech.probabilities.resize(bytes / sizeof(float));
  std::memcpy(speech.probabilities.data(), text.data() + end + 1, bytes);
  return speech;
}

void SpeechFile::Save(const DetectedSpeech& speech) {
  const Json header = {{"format", kFormat}, {"library", library_}, {"key", speech.key}};
  const std::string line = header.dump() + "\n";
  {
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    out.write(line.data(), static_cast<std::streamsize>(line.size()));
    out.write(reinterpret_cast<const char*>(speech.probabilities.data()),
              static_cast<std::streamsize>(speech.probabilities.size() * sizeof(float)));
    out.flush();
    if (out) return;
  }
  std::error_code ignored;
  std::filesystem::remove(path_, ignored);
}

}  // namespace sillok
