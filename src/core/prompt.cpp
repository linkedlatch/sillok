#include "core/prompt.hpp"

#include "core/words.hpp"

namespace sillok {

PromptBuilder::PromptBuilder(const std::vector<std::string>& glossary,
                             int max_tokens, TokenCounter count_tokens)
    : max_tokens_(max_tokens), count_tokens_(std::move(count_tokens)) {
  for (const std::string& term : glossary) {
    if (term.empty()) continue;
    std::string extended = glossary_.empty() ? term : glossary_ + ", " + term;
    if (count_tokens_(extended) > max_tokens_) continue;
    glossary_ = std::move(extended);
  }
}

std::string PromptBuilder::Build(std::string_view previous_text,
                                 std::string_view language) const {
  const std::string separator = glossary_.empty() ? "" : " ";
  const int budget = max_tokens_ - count_tokens_(glossary_ + separator);
  if (previous_text.empty() || budget <= 0) return glossary_;

  // Positions where the kept context may start: after a space, or at any
  // character in languages without spaces.
  std::vector<std::size_t> starts;
  for (std::size_t i = 0; i < previous_text.size(); ++i) {
    const auto byte = static_cast<unsigned char>(previous_text[i]);
    const bool boundary = UsesSpaces(language)
                              ? i > 0 && previous_text[i - 1] == ' ' && byte != ' '
                              : (byte & 0xC0) != 0x80;
    if (i == 0 || boundary) starts.push_back(i);
  }
  // Token count only falls as the start moves right: binary search for the
  // earliest start that fits.
  std::size_t lo = 0;
  std::size_t hi = starts.size();
  while (lo < hi) {
    const std::size_t mid = (lo + hi) / 2;
    if (count_tokens_(previous_text.substr(starts[mid])) <= budget) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  if (lo == starts.size()) return glossary_;
  return glossary_ + separator + std::string(previous_text.substr(starts[lo]));
}

}  // namespace sillok
