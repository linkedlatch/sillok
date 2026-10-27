#include "core/utf8.hpp"

namespace sillok {
namespace {

constexpr std::string_view kReplacement = "\xEF\xBF\xBD";  // U+FFFD

unsigned char Byte(std::string_view text, std::size_t i) {
  return static_cast<unsigned char>(text[i]);
}

// Length of the sequence a lead byte starts, or 0 if it cannot start one.
std::size_t SequenceLength(unsigned char lead) {
  if (lead < 0x80) return 1;
  if (lead >= 0xC2 && lead <= 0xDF) return 2;
  if (lead >= 0xE0 && lead <= 0xEF) return 3;
  if (lead >= 0xF0 && lead <= 0xF4) return 4;
  return 0;
}

// Whether `byte` may follow `lead` as the second byte of a sequence
// (Unicode Table 3-7); later bytes are always 80..BF.
bool ValidSecond(unsigned char lead, unsigned char byte) {
  switch (lead) {
    case 0xE0: return byte >= 0xA0 && byte <= 0xBF;
    case 0xED: return byte >= 0x80 && byte <= 0x9F;
    case 0xF0: return byte >= 0x90 && byte <= 0xBF;
    case 0xF4: return byte >= 0x80 && byte <= 0x8F;
    default: return byte >= 0x80 && byte <= 0xBF;
  }
}

// Number of bytes at `i` that form a well-formed prefix of a sequence, and
// the full length that sequence needs. prefix == needed means complete.
struct Prefix {
  std::size_t prefix = 0;
  std::size_t needed = 0;
};

Prefix ScanSequence(std::string_view text, std::size_t i) {
  const unsigned char lead = Byte(text, i);
  const std::size_t needed = SequenceLength(lead);
  if (needed == 0) return {0, 0};
  std::size_t prefix = 1;
  while (prefix < needed && i + prefix < text.size()) {
    const unsigned char byte = Byte(text, i + prefix);
    const bool ok = prefix == 1 ? ValidSecond(lead, byte)
                                : byte >= 0x80 && byte <= 0xBF;
    if (!ok) break;
    ++prefix;
  }
  return {prefix, needed};
}

}  // namespace

std::string SanitizeUtf8(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  std::size_t i = 0;
  while (i < text.size()) {
    const Prefix scan = ScanSequence(text, i);
    if (scan.needed != 0 && scan.prefix == scan.needed) {
      out.append(text.substr(i, scan.needed));
      i += scan.needed;
    } else {
      out.append(kReplacement);
      i += scan.prefix == 0 ? 1 : scan.prefix;
    }
  }
  return out;
}

std::size_t IncompleteUtf8Tail(std::string_view text) {
  // The last sequence starts at the last byte that is not 80..BF.
  const std::size_t limit = text.size() < 4 ? text.size() : 4;
  for (std::size_t back = 1; back <= limit; ++back) {
    const std::size_t i = text.size() - back;
    const unsigned char byte = Byte(text, i);
    if (byte >= 0x80 && byte <= 0xBF) continue;
    const Prefix scan = ScanSequence(text, i);
    const bool unfinished = scan.needed > back && scan.prefix == back;
    return unfinished ? back : 0;
  }
  return 0;
}

std::size_t CountCodePoints(std::string_view text) {
  std::size_t count = 0;
  for (const char c : text) {
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++count;
  }
  return count;
}

}  // namespace sillok
