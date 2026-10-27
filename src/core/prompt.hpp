#ifndef SILLOK_CORE_PROMPT_HPP_
#define SILLOK_CORE_PROMPT_HPP_

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sillok {

// Builds the prompt for each window: the user's glossary, then as much of
// the preceding text as still fits the token budget.
class PromptBuilder {
 public:
  using TokenCounter = std::function<int(std::string_view)>;

  PromptBuilder(const std::vector<std::string>& glossary, int max_tokens,
                TokenCounter count_tokens);

  // `previous_text` is in `language`; pass an empty one when it must not be
  // used as context.
  std::string Build(std::string_view previous_text,
                    std::string_view language) const;

 private:
  int max_tokens_;
  TokenCounter count_tokens_;
  std::string glossary_;  // the terms that fit, in order, joined by ", "
};

}  // namespace sillok

#endif  // SILLOK_CORE_PROMPT_HPP_
