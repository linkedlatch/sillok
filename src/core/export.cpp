#include "core/export.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string_view>

#include "core/audio.hpp"

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

// Text with blank lines collapsed: in SubRip a blank line ends the cue.
std::string WithoutBlankLines(std::string_view text) {
  std::string out;
  for (const char c : text) {
    if (c == '\n' && (out.empty() || out.back() == '\n')) continue;
    out.push_back(c);
  }
  while (!out.empty() && out.back() == '\n') out.pop_back();
  return out;
}

std::string SrtTime(Milliseconds time) {
  const std::int64_t ms = std::max<std::int64_t>(0, time.count());
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%02d:%02d:%02d,%03d",
                static_cast<int>(ms / 3600000),
                static_cast<int>(ms / 60000 % 60),
                static_cast<int>(ms / 1000 % 60), static_cast<int>(ms % 1000));
  return buffer;
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

std::string ToSrt(const Transcript& transcript, const ExportOptions& options) {
  const bool labeled = HasSeveralSpeakers(transcript);
  std::string out;
  int index = 0;
  for (const Segment& segment : transcript.segments) {
    out += std::to_string(++index) + '\n';
    out += SrtTime(segment.start) + " --> " + SrtTime(segment.end) + '\n';
    out += WithoutBlankLines(LineText(transcript, segment, labeled, options));
    out += "\n\n";
  }
  return out;
}

}  // namespace sillok
