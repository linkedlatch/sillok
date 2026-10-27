#include "core/files.hpp"

#include <fstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "core/sha256.hpp"

namespace sillok {

std::filesystem::path PathFromUtf8(std::string_view utf8) {
  std::u8string text(utf8.size(), u8'\0');
  for (std::size_t i = 0; i < utf8.size(); ++i) {
    text[i] = static_cast<char8_t>(utf8[i]);
  }
  return std::filesystem::path(text);
}

std::string PathToUtf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  std::string out(text.size(), '\0');
  for (std::size_t i = 0; i < text.size(); ++i) {
    out[i] = static_cast<char>(text[i]);
  }
  return out;
}

std::string Sha256OfFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + PathToUtf8(path));
  Sha256 hash;
  std::vector<char> buffer(std::size_t{1} << 20);
  while (in) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = static_cast<std::size_t>(in.gcount());
    hash.Update({reinterpret_cast<const unsigned char*>(buffer.data()), count});
  }
  if (in.bad()) throw std::runtime_error("cannot read " + PathToUtf8(path));
  return hash.HexDigest();
}

FileCheck CheckFile(const std::filesystem::path& path, std::uint64_t size,
                    std::string_view sha256, bool verify_hash) {
  std::error_code error;
  const std::uintmax_t actual = std::filesystem::file_size(path, error);
  if (error) return FileCheck::kMissing;
  if (actual != size) return FileCheck::kWrongSize;
  if (verify_hash && Sha256OfFile(path) != sha256) return FileCheck::kWrongHash;
  return FileCheck::kOk;
}

}  // namespace sillok
