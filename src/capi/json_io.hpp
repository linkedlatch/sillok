#ifndef SILLOK_CAPI_JSON_IO_HPP_
#define SILLOK_CAPI_JSON_IO_HPP_

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

EngineConfig ParseEngineConfig(std::string_view json);
PipelineOptions ParseJobOptions(std::string_view json);
// Empty input gives the defaults.
ExportOptions ParseExportOptions(std::string_view json);

std::string TranscriptToJson(const Transcript& transcript);
Transcript TranscriptFromJson(std::string_view json);

std::string CatalogToJson(const std::vector<CatalogEntry>& catalog);
std::string DevicesToJson(const std::vector<DeviceInfo>& devices);

// State of one catalog file in a models directory.
struct CatalogStatus {
  const CatalogEntry* entry = nullptr;
  // "ok", "missing", "wrong_size", "wrong_hash", or for archives also
  // "extract" (the archive is valid but not extracted yet).
  std::string status;
};
std::string CatalogStatusToJson(const std::vector<CatalogStatus>& statuses);

}  // namespace sillok

#endif  // SILLOK_CAPI_JSON_IO_HPP_
