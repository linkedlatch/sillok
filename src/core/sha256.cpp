#include "core/sha256.hpp"

#include <bit>

namespace sillok {
namespace {

constexpr std::array<std::uint32_t, 64> kRound = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

}  // namespace

void Sha256::Update(std::span<const unsigned char> data) {
  length_ += data.size();
  for (const unsigned char byte : data) {
    buffer_[buffered_++] = byte;
    if (buffered_ == buffer_.size()) {
      Compress(buffer_.data());
      buffered_ = 0;
    }
  }
}

std::string Sha256::HexDigest() {
  const std::uint64_t bits = length_ * 8;
  const unsigned char pad = 0x80;
  Update({&pad, 1});
  const unsigned char zero = 0;
  while (buffered_ != 56) Update({&zero, 1});
  std::array<unsigned char, 8> encoded_length;
  for (std::size_t i = 0; i < 8; ++i) {
    encoded_length[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
  }
  Update(encoded_length);

  constexpr char kHex[] = "0123456789abcdef";
  std::string hex;
  for (const std::uint32_t word : state_) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      hex.push_back(kHex[(word >> shift) & 0xF]);
    }
  }
  return hex;
}

void Sha256::Compress(const unsigned char* block) {
  std::array<std::uint32_t, 64> w;
  for (std::size_t i = 0; i < 16; ++i) {
    w[i] = static_cast<std::uint32_t>(block[4 * i]) << 24 |
           static_cast<std::uint32_t>(block[4 * i + 1]) << 16 |
           static_cast<std::uint32_t>(block[4 * i + 2]) << 8 |
           static_cast<std::uint32_t>(block[4 * i + 3]);
  }
  for (std::size_t i = 16; i < 64; ++i) {
    const std::uint32_t s0 = std::rotr(w[i - 15], 7) ^
                             std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const std::uint32_t s1 = std::rotr(w[i - 2], 17) ^
                             std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  std::array<std::uint32_t, 8> v = state_;
  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint32_t s1 =
        std::rotr(v[4], 6) ^ std::rotr(v[4], 11) ^ std::rotr(v[4], 25);
    const std::uint32_t choice = (v[4] & v[5]) ^ (~v[4] & v[6]);
    const std::uint32_t t1 = v[7] + s1 + choice + kRound[i] + w[i];
    const std::uint32_t s0 =
        std::rotr(v[0], 2) ^ std::rotr(v[0], 13) ^ std::rotr(v[0], 22);
    const std::uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
    const std::uint32_t t2 = s0 + majority;
    v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
  }
  for (std::size_t i = 0; i < 8; ++i) state_[i] += v[i];
}

}  // namespace sillok
