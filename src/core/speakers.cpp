#include "core/speakers.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <utility>

#include "core/words.hpp"

namespace sillok {
namespace {

// Speaker overlapping `word` the most, else the nearest within `max_distance`.
// `turns` is sorted by begin; `longest_turn` is the longest of them.
std::optional<int> SpeakerOf(const Word& word,
                             std::span<const SpeakerTurn> turns,
                             SampleIndex longest_turn,
                             SampleIndex max_distance) {
  const SampleIndex begin = ToSamples(word.start);
  const SampleIndex end = std::max(ToSamples(word.end), begin + 1);
  std::map<int, SampleIndex> overlap;
  std::optional<int> nearest;
  SampleIndex nearest_distance = max_distance + 1;

  auto it = std::upper_bound(turns.begin(), turns.end(), end + max_distance,
                             [](SampleIndex value, const SpeakerTurn& t) {
                               return value < t.range.begin;
                             });
  while (it != turns.begin()) {
    const SpeakerTurn& turn = *--it;
    if (turn.range.begin + longest_turn < begin - max_distance) break;
    const SampleIndex shared =
        std::min(end, turn.range.end) - std::max(begin, turn.range.begin);
    if (shared > 0) {
      overlap[turn.speaker] += shared;
      continue;
    }
    const SampleIndex distance =
        std::max(turn.range.begin - end, begin - turn.range.end);
    if (distance < nearest_distance) {
      nearest = turn.speaker;
      nearest_distance = distance;
    }
  }
  if (overlap.empty()) return nearest;
  return std::max_element(
             overlap.begin(), overlap.end(),
             [](const auto& a, const auto& b) { return a.second < b.second; })
      ->first;
}

}  // namespace

std::vector<Segment> AssignSpeakers(std::vector<Segment> segments,
                                    std::span<const SpeakerTurn> turns,
                                    const SpeakerOptions& options) {
  std::vector<SpeakerTurn> sorted(turns.begin(), turns.end());
  std::sort(sorted.begin(), sorted.end(),
            [](const SpeakerTurn& a, const SpeakerTurn& b) {
              return a.range.begin < b.range.begin;
            });
  SampleIndex longest_turn = 0;
  for (const SpeakerTurn& turn : sorted) {
    longest_turn = std::max(longest_turn, turn.range.length());
  }

  std::vector<Word*> words;
  for (Segment& segment : segments) {
    for (Word& word : segment.words) words.push_back(&word);
  }
  for (Word* word : words) {
    word->speaker =
        SpeakerOf(*word, sorted, longest_turn, ToSamples(options.max_distance));
  }
  // Words between turns take the previous (or else the next) known speaker.
  std::optional<int> last_speaker;
  for (Word* word : words) {
    if (word->speaker) {
      last_speaker = word->speaker;
    } else {
      word->speaker = last_speaker;
    }
  }
  last_speaker.reset();
  for (auto it = words.rbegin(); it != words.rend(); ++it) {
    if ((*it)->speaker) {
      last_speaker = (*it)->speaker;
    } else {
      (*it)->speaker = last_speaker;
    }
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
