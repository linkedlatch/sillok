#ifndef SILLOK_CORE_SHA256_HPP_
#define SILLOK_CORE_SHA256_HPP_

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace sillok {

// Incremental SHA-256 (FIPS 180-4).
class Sha256 {
 public:
  void Update(std::span<const unsigned char> data);
  // Lowercase hex digest. Finishes the hash; Update is not allowed afterwards.
  std::string HexDigest();

 private:
  void Compress(const unsigned char* block);

  std::array<std::uint32_t, 8> state_ = {
      0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::array<unsigned char, 64> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t length_ = 0;  // bytes hashed so far
};

}  // namespace sillok

#endif  // SILLOK_CORE_SHA256_HPP_
