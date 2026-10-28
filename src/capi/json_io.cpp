#include "capi/json_io.hpp"

#include <initializer_list>
#include <optional>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace sillok {
namespace {

using Json = nlohmann::json;

Json Parse(std::string_view text, std::string_view what) {
  try {
    Json json = Json::parse(text.begin(), text.end());
    if (!json.is_object()) {
      throw std::invalid_argument(std::string(what) + " must be a JSON object");
    }
    return json;
  } catch (const Json::exception& error) {
    throw std::invalid_argument(std::string(what) + ": " + error.what());
  }
}

void RejectUnknownKeys(const Json& object, std::string_view what,
                       std::initializer_list<std::string_view> known) {
  for (const auto& [key, value] : object.items()) {
    bool found = false;
    for (const std::string_view name : known) found = found || key == name;
    if (!found) {
      throw std::invalid_argument("unknown key \"" + key + "\" in " +
                                  std::string(what));
    }
  }
}

// Value of `key` converted to T, or `fallback` when absent.
template <typename T>
T Get(const Json& object, const char* key, T fallback) {
  const auto it = object.find(key);
  if (it == object.end() || it->is_null()) return fallback;
  try {
    return it->get<T>();
  } catch (const Json::exception&) {
    throw std::invalid_argument(std::string("\"") + key +
                                "\" has the wrong type");
  }
}

std::string Dump(const Json& json) {
  return json.dump(-1, ' ', false, Json::error_handler_t::replace);
}

long long Ms(Millis time) { return time.count(); }

Millis ReadMs(const Json& object, const char* key) {
  const auto value = Get<long long>(object, key, -1);
  if (value < 0) {
    throw std::invalid_argument(std::string("\"") + key +
                                "\" must be a non-negative integer");
  }
  return Millis{value};
}

std::optional<int> ReadSpeaker(const Json& object) {
  const auto it = object.find("speaker");
  if (it == object.end() || it->is_null()) return std::nullopt;
  if (!it->is_number_integer()) {
    throw std::invalid_argument("\"speaker\" must be an integer");
  }
  return it->get<int>();
}

}  // namespace

EngineSettings ParseEngineConfig(std::string_view text) {
  const Json json = Parse(text, "engine config");
  RejectUnknownKeys(json, "engine config",
                    {"models_dir", "device", "gpu_index", "threads", "temp_dir"});
  EngineSettings settings;
  settings.temp_dir = PathFromUtf8(Get<std::string>(json, "temp_dir", ""));
  EngineConfig& config = settings.models;
  const auto models_dir = Get<std::string>(json, "models_dir", "");
  if (models_dir.empty()) throw std::invalid_argument("\"models_dir\" is required");
  config.models_dir = PathFromUtf8(models_dir);
  const auto device = Get<std::string>(json, "device", "cpu");
  if (device == "cpu") {
    config.device = Device::kCpu;
  } else if (device == "gpu") {
    config.device = Device::kGpu;
  } else {
    throw std::invalid_argument("\"device\" must be \"cpu\" or \"gpu\"");
  }
  config.gpu_index = Get<int>(json, "gpu_index", 0);
  config.threads = Get<int>(json, "threads", 0);
  if (config.gpu_index < 0 || config.threads < 0) {
    throw std::invalid_argument("\"gpu_index\" and \"threads\" must be >= 0");
  }
  return settings;
}

PipelineOptions ParseJobOptions(std::string_view text) {
  PipelineOptions options;
  if (text.empty()) return options;
  const Json json = Parse(text, "job options");
  RejectUnknownKeys(json, "job options", {"language", "diarize", "glossary"});
  options.language = Get<std::string>(json, "language", "auto");
  if (options.language != "auto" && options.language != "ko" &&
      options.language != "en") {
    throw std::invalid_argument("\"language\" must be \"auto\", \"ko\" or \"en\"");
  }
  options.diarize = Get<bool>(json, "diarize", false);
  options.glossary = Get<std::vector<std::string>>(json, "glossary", {});
  return options;
}

ExportOptions ParseExportOptions(std::string_view text) {
  ExportOptions options;
  if (text.empty()) return options;
  const Json json = Parse(text, "export options");
  RejectUnknownKeys(json, "export options", {"speaker_label"});
  options.speaker_label =
      Get<std::string>(json, "speaker_label", options.speaker_label);
  return options;
}

std::string TranscriptToJson(const Transcript& transcript) {
  std::set<int> speaker_ids;
  Json segments = Json::array();
  for (const Segment& segment : transcript.segments) {
    Json words = Json::array();
    for (const Word& word : segment.words) {
      Json item = {{"text", word.text},
                   {"start_ms", Ms(word.start)},
                   {"end_ms", Ms(word.end)},
                   {"confidence", word.confidence}};
      if (word.speaker) item["speaker"] = *word.speaker;
      words.push_back(std::move(item));
    }
    Json item = {{"start_ms", Ms(segment.start)},
                 {"end_ms", Ms(segment.end)},
                 {"text", segment.text},
                 {"language", segment.language},
                 {"words", std::move(words)}};
    if (segment.speaker) {
      item["speaker"] = *segment.speaker;
      speaker_ids.insert(*segment.speaker);
    }
    segments.push_back(std::move(item));
  }
  for (const auto& [id, name] : transcript.speaker_names) speaker_ids.insert(id);
  Json speakers = Json::array();
  for (const int id : speaker_ids) {
    const auto named = transcript.speaker_names.find(id);
    speakers.push_back(
        {{"id", id},
         {"name", named == transcript.speaker_names.end() ? Json(nullptr)
                                                          : Json(named->second)}});
  }
  Json models = Json::object();
  for (const auto& [stage, info] : transcript.models) {
    models[stage] = {{"name", info.name},
                     {"device", info.device},
                     {"fell_back_to_cpu", info.fell_back_to_cpu}};
  }
  return Dump({{"version", 1},
               {"duration_ms", Ms(transcript.duration)},
               {"speakers", std::move(speakers)},
               {"segments", std::move(segments)},
               {"models", std::move(models)}});
}

Transcript TranscriptFromJson(std::string_view text) {
  const Json json = Parse(text, "transcript");
  if (Get<int>(json, "version", 0) != 1) {
    throw std::invalid_argument("unsupported transcript version");
  }
  Transcript transcript;
  transcript.duration = Millis{Get<long long>(json, "duration_ms", 0)};
  for (const Json& speaker : Get<Json>(json, "speakers", Json::array())) {
    const auto name = Get<std::string>(speaker, "name", "");
    if (!name.empty()) transcript.speaker_names[Get<int>(speaker, "id", -1)] = name;
  }
  for (const Json& item : Get<Json>(json, "segments", Json::array())) {
    Segment segment;
    segment.start = ReadMs(item, "start_ms");
    segment.end = ReadMs(item, "end_ms");
    segment.text = Get<std::string>(item, "text", "");
    segment.language = Get<std::string>(item, "language", "");
    segment.speaker = ReadSpeaker(item);
    for (const Json& word : Get<Json>(item, "words", Json::array())) {
      segment.words.push_back({.text = Get<std::string>(word, "text", ""),
                               .start = ReadMs(word, "start_ms"),
                               .end = ReadMs(word, "end_ms"),
                               .confidence = Get<float>(word, "confidence", 0.0f),
                               .speaker = ReadSpeaker(word)});
    }
    transcript.segments.push_back(std::move(segment));
  }
  const Json models = Get<Json>(json, "models", Json::object());
  for (const auto& [stage, info] : models.items()) {
    transcript.models[stage] = {
        .name = Get<std::string>(info, "name", ""),
        .device = Get<std::string>(info, "device", ""),
        .fell_back_to_cpu = Get<bool>(info, "fell_back_to_cpu", false)};
  }
  return transcript;
}

std::string CatalogToJson(const std::vector<CatalogEntry>& catalog) {
  Json entries = Json::array();
  for (const CatalogEntry& entry : catalog) {
    entries.push_back({{"role", entry.role},
                       {"file", entry.file},
                       {"url", entry.url},
                       {"size", entry.size},
                       {"sha256", entry.sha256},
                       {"zip", entry.zip}});
  }
  return Dump(entries);
}

std::string DevicesToJson(const std::vector<DeviceInfo>& devices) {
  Json entries = Json::array();
  for (const DeviceInfo& device : devices) {
    entries.push_back({{"name", device.name},
                       {"description", device.description},
                       {"type", device.type},
                       {"memory_total", device.memory_total},
                       {"memory_free", device.memory_free}});
  }
  return Dump(entries);
}

std::string CatalogStatusToJson(const std::vector<CatalogStatus>& statuses) {
  Json entries = Json::array();
  for (const CatalogStatus& status : statuses) {
    entries.push_back({{"role", status.entry->role},
                       {"file", status.entry->file},
                       {"status", status.status}});
  }
  return Dump(entries);
}

}  // namespace sillok
