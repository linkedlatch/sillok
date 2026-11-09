#include "core/speakers.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <utility>

#include "core/words.hpp"

namespace sillok {
namespace {

// Turns sorted by begin, with what finding the speaker of a word needs.
class Turns {
 public:
  explicit Turns(std::span<const SpeakerTurn> turns) : turns_(turns.begin(), turns.end()) {
    std::sort(turns_.begin(), turns_.end(), [](const SpeakerTurn& a, const SpeakerTurn& b) {
      return a.range.begin < b.range.begin;
    });
    for (std::size_t i = 0; i < turns_.size(); ++i) {
      longest_ = std::max(longest_, turns_[i].range.length());
      const bool later_end =
          i == 0 || turns_[i].range.end > turns_[latest_end_.back()].range.end;
      latest_end_.push_back(later_end ? i : latest_end_.back());
    }
  }

  // Speaker overlapping [begin, end) the most, else that of the nearest turn.
  std::optional<int> SpeakerOf(SampleIndex begin, SampleIndex end) const {
    if (turns_.empty()) return std::nullopt;
    // Turns from `after` on begin at or after `end`.
    const auto after = std::lower_bound(
        turns_.begin(), turns_.end(), end,
        [](const SpeakerTurn& turn, SampleIndex value) { return turn.range.begin < value; });
    std::map<int, SampleIndex> overlap;
    for (auto it = after; it != turns_.begin();) {
      const SpeakerTurn& turn = *--it;
      if (turn.range.begin + longest_ <= begin) break;  // none earlier reaches the word
      const SampleIndex shared = std::min(end, turn.range.end) - std::max(begin, turn.range.begin);
      if (shared > 0) overlap[turn.speaker] += shared;
    }
    if (!overlap.empty()) {
      return std::max_element(overlap.begin(), overlap.end(),
                              [](const auto& a, const auto& b) { return a.second < b.second; })
          ->first;
    }
    // No turn overlaps: the one ending last before the word or the first
    // beginning after it, whichever is closer.
    const std::optional<SpeakerTurn> before =
        after == turns_.begin()
            ? std::nullopt
            : std::optional(turns_[latest_end_[static_cast<std::size_t>(after - turns_.begin()) - 1]]);
    if (after == turns_.end()) return before->speaker;
    if (!before) return after->speaker;
    return begin - before->range.end <= after->range.begin - end ? before->speaker
                                                                  : after->speaker;
  }

 private:
  std::vector<SpeakerTurn> turns_;
  std::vector<std::size_t> latest_end_;  // turn ending last among turns_[0..i]
  SampleIndex longest_ = 0;
};

}  // namespace

std::vector<Segment> AssignSpeakers(std::vector<Segment> segments,
                                    std::span<const SpeakerTurn> turns) {
  const Turns sorted(turns);
  std::vector<Word*> words;
  for (Segment& segment : segments) {
    for (Word& word : segment.words) words.push_back(&word);
  }
  for (Word* word : words) {
    const SampleIndex begin = ToSamples(word->start);
    word->speaker = sorted.SpeakerOf(begin, std::max(ToSamples(word->end), begin + 1));
  }

  // Number speakers by first appearance.
  std::map<int, int> numbering;
  for (Word* word : words) {
    if (!word->speaker) continue;
    const auto [it, inserted] = numbering.try_emplace(
        *word->speaker, static_cast<int>(numbering.size()));
    word->speaker = it->second;
  }

  std::vector<Segment> out;
  for (Segment& segment : segments) {
    std::vector<Word> run;
    for (Word& word : segment.words) {
      if (!run.empty() && run.back().speaker != word.speaker) {
        out.push_back(MakeSegment(std::move(run), segment.language));
        run.clear();
      }
      run.push_back(std::move(word));
    }
    if (!run.empty())
      out.push_back(MakeSegment(std::move(run), segment.language));
  }
  return out;
}

}  // namespace sillok
