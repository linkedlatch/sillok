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

constexpr int kFormat = 2;

}  // namespace

CheckpointFile::CheckpointFile(std::filesystem::path path, std::string library)
    : path_(std::move(path)), library_(std::move(library)) {}

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
    const std::string line = text.substr(begin, end - begin);
    try {
      if (!header) {
        const Json json = Json::parse(line);
        if (json.value("format", 0) != kFormat || json.value("library", "") != library_) {
          return std::nullopt;
        }
        checkpoint.key = json.at("key").get<std::string>();
        header = true;
      } else {
        const std::vector<Segment> segments = SegmentsFromJson(line);
        checkpoint.segments.insert(checkpoint.segments.end(), segments.begin(), segments.end());
        ++checkpoint.groups;
      }
    } catch (const std::exception&) {
      break;  // a damaged line: keep the groups before it
    }
    kept.append(text, begin, end + 1 - begin);
    begin = end + 1;
  }
  if (!header) return std::nullopt;
  if (kept.size() != text.size()) Write(kept, false);
  return checkpoint;
}

void CheckpointFile::Start(const std::string& key) {
  failed_ = false;
  const Json header = {
      {"format", kFormat}, {"library", library_}, {"key", key}};
  Write(header.dump() + "\n", false);
}

void CheckpointFile::AddGroup(std::span<const Segment> segments) {
  Write(SegmentsToJson(segments) + "\n", true);
}

void CheckpointFile::Remove() {
  std::error_code ignored;
  std::filesystem::remove(path_, ignored);
}

void CheckpointFile::Write(const std::string& text, bool append) {
  if (failed_) return;
  std::ofstream out(path_, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  out.flush();
  if (out) return;
  failed_ = true;
  out.close();
  Remove();
}

}  // namespace sillok
