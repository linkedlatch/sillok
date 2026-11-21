#ifndef SILLOK_CAPI_CHECKPOINT_HPP_
#define SILLOK_CAPI_CHECKPOINT_HPP_

#include <filesystem>
#include <optional>
#include <string>

#include "core/pipeline.hpp"

namespace sillok {

// A job's checkpoint file (the job option "checkpoint"), in JSON Lines: a
// header with the format, the library, the engine's transcription model, the
// key and the chunks' limit, then a line for each model that ran and for each
// chunk a stage finished. Lines are appended as chunks finish, so a run that
// stops at any point leaves every finished chunk behind, and a last line cut
// short by a crash is dropped. Writing is best effort: when it fails the file
// is removed, and the job carries on without one.
class CheckpointFile final : public CheckpointSink {
 public:
  // `library` names the library version and `models` the engine's
  // transcription model: a file written by another of either is not
  // continued.
  CheckpointFile(std::filesystem::path path, std::string library, std::string models);

  // What an earlier run left, if anything. Drops a last line cut short from
  // the file, so that lines can be appended to it.
  std::optional<Checkpoint> Load();

  void Start(const std::string& key, Milliseconds max_chunk) override;
  void AddChunk(Stage stage, std::size_t chunk, const ChunkResult& result) override;
  void AddModel(const std::string& stage, const ModelInfo& info) override;

  // Removes the file, once the job no longer needs it.
  void Remove();

 private:
  void Write(const std::string& text, bool append);

  std::filesystem::path path_;
  std::string library_;
  std::string models_;
  bool failed_ = false;
};

}  // namespace sillok

#endif  // SILLOK_CAPI_CHECKPOINT_HPP_
