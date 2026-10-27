#ifndef SILLOK_CORE_UTF8_HPP_
#define SILLOK_CORE_UTF8_HPP_

#include <cstddef>
#include <string>
#include <string_view>

namespace sillok {

// Copy of `text` in which every ill-formed UTF-8 sequence is replaced by
// U+FFFD (one replacement per maximal invalid subpart).
std::string SanitizeUtf8(std::string_view text);

// Length (0 to 3) of a trailing UTF-8 sequence that is well formed so far but
// unfinished, i.e. bytes that a following chunk may complete.
std::size_t IncompleteUtf8Tail(std::string_view text);

// Number of code points in well-formed UTF-8 `text`.
std::size_t CountCodePoints(std::string_view text);

}  // namespace sillok

#endif  // SILLOK_CORE_UTF8_HPP_
