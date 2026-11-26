#ifndef SILLOK_CAPI_JSON_IO_HPP_
#define SILLOK_CAPI_JSON_IO_HPP_

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "backends/models.hpp"
#include "core/export.hpp"
#include "core/files.hpp"
#include "core/pipeline.hpp"
#include "core/transcript.hpp"

// JSON documents of the C ABI. Malformed input throws std::invalid_argument.

namespace sillok {

// Engine configuration of the C ABI.
EngineConfig ParseEngineConfig(std::string_view json);

// Job options of the C ABI.
struct JobOptions {
  PipelineOptions pipeline;
  // Whether pushed audio has its loudness evened out; not for audio that
  // already has, such as a job's copy for listening back.
  bool even_loudness = true;
  std::filesystem::path checkpoint;  // empty for none
  std::filesystem::path speech;      // empty for none
};

JobOptions ParseJobOptions(std::string_view json);
// Empty input gives the defaults.
ExportOptions ParseExportOptions(std::string_view json);

std::string TranscriptToJson(const Transcript& transcript);
// Segments as in a transcript document, as an array.
std::string SegmentsToJson(std::span<const Segment> segments);
std::vector<Segment> SegmentsFromJson(std::string_view json);
// A preview's segments as the segment callback gets them: the array of
// SegmentsToJson, each with its "chunk", and "draft": true for a draft.
std::string PreviewToJson(const Preview& preview);
Transcript TranscriptFromJson(std::string_view json);

std::string CatalogToJson(const std::vector<CatalogModel>& catalog);
std::string DevicesToJson(const std::vector<DeviceInfo>& devices);

// State of one catalog file in a models directory.
struct CatalogStatus {
  const CatalogModel* model = nullptr;
  const CatalogFile* file = nullptr;
  // "ok", "missing", "wrong_size", "wrong_hash", or for archives also
  // "extract" (the archive is valid but not extracted yet).
  std::string status;
};
std::string CatalogStatusToJson(const std::vector<CatalogStatus>& statuses);

}  // namespace sillok

#endif  // SILLOK_CAPI_JSON_IO_HPP_
