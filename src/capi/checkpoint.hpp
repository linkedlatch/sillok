#ifndef SILLOK_CAPI_CHECKPOINT_HPP_
#define SILLOK_CAPI_CHECKPOINT_HPP_

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/pipeline.hpp"

namespace sillok {

// A job's checkpoint file (the job option "checkpoint"): a line with the
// library, the key and the language, then one line per transcribed group with
// its segments as JSON. Lines are appended as groups finish, so a run that
// stops at any point leaves every finished group behind, and a last line cut
// short by a crash is dropped. Writing is best effort: when it fails the file
// is removed, and the job carries on without one.
class CheckpointFile final : public CheckpointSink {
 public:
  // `library` names the library version; a file another version wrote is not
  // continued.
  CheckpointFile(std::filesystem::path path, std::string library);

  // What an earlier run of this library left, if anything. Drops a last line
  // cut short from the file, so that groups can be appended to it.
  std::optional<Checkpoint> Load();

  void Start(const std::string& key, const std::string& language) override;
  void AddGroup(std::span<const Segment> segments) override;

  // Removes the file, once the job no longer needs it.
  void Remove();

 private:
  void Write(const std::string& text, bool append);

  std::filesystem::path path_;
  std::string library_;
  bool failed_ = false;
};

}  // namespace sillok

#endif  // SILLOK_CAPI_CHECKPOINT_HPP_
