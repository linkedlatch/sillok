#ifndef SILLOK_CORE_EXPORT_HPP_
#define SILLOK_CORE_EXPORT_HPP_

#include <string>

#include "core/transcript.hpp"

namespace sillok {

struct ExportOptions {
  // Label for a speaker without a name; "{n}" becomes the 1-based number.
  std::string speaker_label = "Speaker {n}";
};

// Display label of speaker `id`: its name if the app set one, else the
// numbered label.
std::string SpeakerLabel(const Transcript& transcript, int id,
                         const ExportOptions& options);

// One line per segment. Speaker labels appear only with two or more speakers.
std::string ToText(const Transcript& transcript, const ExportOptions& options);
// SubRip subtitles; same labeling rule as ToText.
std::string ToSrt(const Transcript& transcript, const ExportOptions& options);

}  // namespace sillok

#endif  // SILLOK_CORE_EXPORT_HPP_
