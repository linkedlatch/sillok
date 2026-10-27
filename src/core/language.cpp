#include "core/language.hpp"

#include <algorithm>
#include <stdexcept>

namespace sillok {

LanguageStabilizer::LanguageStabilizer(std::vector<std::string> candidates,
                                       LanguageOptions options)
    : candidates_(std::move(candidates)), options_(options) {
  if (candidates_.empty()) {
    throw std::invalid_argument("no candidate languages");
  }
}

LanguageDecision LanguageStabilizer::Decide(
    std::span<const LanguageScore> scores) {
  // Probabilities restricted to the candidates and renormalized.
  std::vector<float> p(candidates_.size(), 0.0f);
  float total = 0.0f;
  for (const LanguageScore& score : scores) {
    const auto it =
        std::find(candidates_.begin(), candidates_.end(), score.language);
    if (it == candidates_.end()) continue;
    p[static_cast<std::size_t>(it - candidates_.begin())] += score.probability;
    total += score.probability;
  }
  if (total > 0.0f) {
    for (float& value : p) value /= total;
  }
  const auto index_of = [&](const std::string& language) {
    return static_cast<std::size_t>(
        std::find(candidates_.begin(), candidates_.end(), language) -
        candidates_.begin());
  };
  const auto best = static_cast<std::size_t>(
      std::max_element(p.begin(), p.end()) - p.begin());

  std::string chosen;
  if (!current_ || candidates_[best] == *current_) {
    chosen = candidates_[best];
    pending_.reset();
  } else if (p[best] >= options_.switch_now ||
             (p[best] >= options_.switch_confirm &&
              pending_ == candidates_[best])) {
    chosen = candidates_[best];
  } else {
    chosen = *current_;
    pending_ = p[best] >= options_.switch_confirm
                   ? std::optional<std::string>(candidates_[best])
                   : std::nullopt;
  }

  LanguageDecision decision{.language = chosen,
                            .alternative = std::nullopt,
                            .overruled = chosen != candidates_[best]};
  std::size_t runner_up = candidates_.size();
  for (std::size_t i = 0; i < candidates_.size(); ++i) {
    if (candidates_[i] == chosen) continue;
    if (runner_up == candidates_.size() || p[i] > p[runner_up]) runner_up = i;
  }
  if (runner_up != candidates_.size() && p[runner_up] >= options_.retry_min &&
      p[index_of(chosen)] < options_.switch_now) {
    decision.alternative = candidates_[runner_up];
  }
  return decision;
}

void LanguageStabilizer::Commit(const std::string& language) {
  if (current_ != language) pending_.reset();
  current_ = language;
}

}  // namespace sillok
