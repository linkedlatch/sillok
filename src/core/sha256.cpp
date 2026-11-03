#include "core/sha256.hpp"

#include <bit>
#include <cstddef>

namespace sillok {
namespace {

constexpr std::array<std::uint32_t, 64> kK = {
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
  std::uint32_t a = state_[0];
  std::uint32_t b = state_[1];
  std::uint32_t c = state_[2];
  std::uint32_t d = state_[3];
  std::uint32_t e = state_[4];
  std::uint32_t f = state_[5];
  std::uint32_t g = state_[6];
  std::uint32_t h = state_[7];

  for (std::size_t i = 0; i < 64; ++i) {
    if (i < 16) {
      const std::size_t j = i * 4;
      w[i] = static_cast<std::uint32_t>(block[j]) << 24 |
             static_cast<std::uint32_t>(block[j + 1]) << 16 |
             static_cast<std::uint32_t>(block[j + 2]) << 8 |
             static_cast<std::uint32_t>(block[j + 3]);
    } else {
      const std::uint32_t v1 = w[i - 2];
      const std::uint32_t t1 =
          std::rotr(v1, 17) ^ std::rotr(v1, 19) ^ (v1 >> 10);
      const std::uint32_t v2 = w[i - 15];
      const std::uint32_t t2 = std::rotr(v2, 7) ^ std::rotr(v2, 18) ^ (v2 >> 3);
      w[i] = t1 + w[i - 7] + t2 + w[i - 16];
    }

    const std::uint32_t t1 =
        h + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) +
        ((e & f) ^ (~e & g)) + kK[i] + w[i];
    const std::uint32_t t2 =
        (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) +
        ((a & b) ^ (a & c) ^ (b & c));

    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

}  // namespace sillok
