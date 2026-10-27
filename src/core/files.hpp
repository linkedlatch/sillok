#ifndef SILLOK_CORE_FILES_HPP_
#define SILLOK_CORE_FILES_HPP_

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace sillok {

// UTF-8 text <-> path on every platform (std::string paths would use the
// Windows ANSI code page).
std::filesystem::path PathFromUtf8(std::string_view utf8);
std::string PathToUtf8(const std::filesystem::path& path);

// Lowercase hex SHA-256 of a file's contents.
std::string Sha256OfFile(const std::filesystem::path& path);

enum class FileCheck { kOk, kMissing, kWrongSize, kWrongHash };

// Whether `path` holds exactly the expected file. The hash is only computed
// when `verify_hash` is set, since that reads the whole file.
FileCheck CheckFile(const std::filesystem::path& path, std::uint64_t size,
                    std::string_view sha256, bool verify_hash);

}  // namespace sillok

#endif  // SILLOK_CORE_FILES_HPP_
