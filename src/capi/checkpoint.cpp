#include "capi/checkpoint.hpp"

#include <exception>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "capi/json_io.hpp"

namespace sillok {
namespace {

using Json = nlohmann::json;

constexpr int kFormat = 5;

const char* StageName(Stage stage) { return stage == Stage::kAlign ? "align" : "transcribe"; }

}  // namespace

CheckpointFile::CheckpointFile(std::filesystem::path path, std::string library, std::string models)
    : path_(std::move(path)), library_(std::move(library)), models_(std::move(models)) {}

std::optional<Checkpoint> CheckpointFile::Load() {
  std::string text;
  {
    std::ifstream in(path_, std::ios::binary);
    if (!in) return std::nullopt;
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  Checkpoint checkpoint;
  std::string kept;  // the complete lines
  bool header = false;
  std::size_t begin = 0;
  while (begin < text.size()) {
    const std::size_t end = text.find('\n', begin);
    if (end == std::string::npos) break;  // cut short
    try {
      const Json json = Json::parse(text.begin() + static_cast<std::ptrdiff_t>(begin),
                                    text.begin() + static_cast<std::ptrdiff_t>(end));
      if (!header) {
        if (json.value("format", 0) != kFormat || json.value("library", "") != library_ ||
            json.value("models", "") != models_) {
          return std::nullopt;
        }
        checkpoint.key = json.at("key").get<std::string>();
        checkpoint.max_chunk = Milliseconds{json.at("max_chunk_ms").get<Milliseconds::rep>()};
        header = true;
      } else if (json.contains("model")) {
        checkpoint.models[json.at("model").get<std::string>()] = {
            .name = json.at("name").get<std::string>(),
            .device = json.at("device").get<std::string>(),
            .fell_back_to_cpu = json.at("fell_back_to_cpu").get<bool>()};
      } else {
        const std::string stage = json.at("stage").get<std::string>();
        if (stage != "transcribe" && stage != "align") throw std::invalid_argument("stage");
        ChunkResult result{.segments = SegmentsFromJson(json.at("segments").dump()),
                           .text = json.value("text", ""),
                           .language = json.value("language", "")};
        auto& saved = stage == "align" ? checkpoint.aligned : checkpoint.transcribed;
        saved[json.at("chunk").get<std::size_t>()] = std::move(result);
      }
    } catch (const std::exception&) {
      break;  // a damaged line: keep the lines before it
    }
    kept.append(text, begin, end + 1 - begin);
    begin = end + 1;
  }
  if (!header) return std::nullopt;
  if (kept.size() != text.size()) Write(kept, false);
  return checkpoint;
}

void CheckpointFile::Start(const std::string& key, Milliseconds max_chunk) {
  failed_ = false;
  const Json header = {{"format", kFormat},
                       {"library", library_},
                       {"models", models_},
                       {"key", key},
                       {"max_chunk_ms", max_chunk.count()}};
  Write(header.dump() + "\n", false);
}

void CheckpointFile::AddChunk(Stage stage, std::size_t chunk, const ChunkResult& result) {
  Json line = {{"stage", StageName(stage)},
               {"chunk", chunk},
               {"segments", Json::parse(SegmentsToJson(result.segments))}};
  if (!result.text.empty()) {
    line["text"] = result.text;
    line["language"] = result.language;
  }
  Write(line.dump() + "\n", true);
}

void CheckpointFile::AddModel(const std::string& stage, const ModelInfo& info) {
  const Json line = {{"model", stage},
                     {"name", info.name},
                     {"device", info.device},
                     {"fell_back_to_cpu", info.fell_back_to_cpu}};
  Write(line.dump() + "\n", true);
}

void CheckpointFile::Remove() {
  std::error_code ignored;
  std::filesystem::remove(path_, ignored);
}

void CheckpointFile::Write(const std::string& text, bool append) {
  if (failed_) return;
  {
    std::ofstream out(path_, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (out) return;
  }
  failed_ = true;
  Remove();
}

}  // namespace sillok
