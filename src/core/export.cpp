#include "core/export.hpp"

#include <set>

namespace sillok {
namespace {

bool HasSeveralSpeakers(const Transcript& transcript) {
  std::set<int> speakers;
  for (const Segment& segment : transcript.segments) {
    if (segment.speaker) speakers.insert(*segment.speaker);
  }
  return speakers.size() >= 2;
}

std::string LineText(const Transcript& transcript, const Segment& segment,
                     bool labeled, const ExportOptions& options) {
  if (!labeled || !segment.speaker) return segment.text;
  return SpeakerLabel(transcript, *segment.speaker, options) + ": " +
         segment.text;
}

}  // namespace

std::string SpeakerLabel(const Transcript& transcript, int id,
                         const ExportOptions& options) {
  const auto named = transcript.speaker_names.find(id);
  if (named != transcript.speaker_names.end() && !named->second.empty()) {
    return named->second;
  }
  std::string label = options.speaker_label;
  const auto at = label.find("{n}");
  if (at != std::string::npos) label.replace(at, 3, std::to_string(id + 1));
  return label;
}

std::string ToText(const Transcript& transcript, const ExportOptions& options) {
  const bool labeled = HasSeveralSpeakers(transcript);
  std::string out;
  for (const Segment& segment : transcript.segments) {
    out += LineText(transcript, segment, labeled, options);
    out += '\n';
  }
  return out;
}

}  // namespace sillok
