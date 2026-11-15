#include "progress.hpp"

#include <cstdio>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace sillok::cli {
namespace {

constexpr int kReading = 100;  // after sillok_stage's values

const char* StageName(int stage) {
  switch (stage) {
    case kReading: return "reading";
    case SILLOK_STAGE_ANALYZE: return "analyzing";
    case SILLOK_STAGE_TRANSCRIBE: return "transcribing";
    case SILLOK_STAGE_DIARIZE: return "diarizing";
    default: return "working";
  }
}

}  // namespace

bool StderrIsTerminal() {
#ifdef _WIN32
  return _isatty(_fileno(stderr)) != 0;
#else
  return isatty(fileno(stderr)) != 0;
#endif
}

ProgressPrinter::ProgressPrinter(bool quiet)
    : quiet_(quiet), terminal_(StderrIsTerminal()) {}

void ProgressPrinter::Update(sillok_stage stage, double fraction) {
  Show(stage, fraction);
}

void ProgressPrinter::Reading(double fraction) { Show(kReading, fraction); }

void ProgressPrinter::Show(int stage, double fraction) {
  if (quiet_) return;
  const int percent = static_cast<int>(fraction * 100.0 + 0.5);
  if (stage != stage_) {
    if (terminal_ && stage_ >= 0) std::fputc('\n', stderr);
    stage_ = stage;
    percent_ = -1;
  }
  if (terminal_) {
    if (percent == percent_) return;
    std::fprintf(stderr, "\r%-13s %3d%%", StageName(stage_), percent);
  } else {
    const int step = percent / 10 * 10;
    if (percent_ >= 0 && step == percent_) return;
    std::fprintf(stderr, "%s %d%%\n", StageName(stage_), step);
    percent_ = step;
    std::fflush(stderr);
    return;
  }
  percent_ = percent;
  std::fflush(stderr);
}

void ProgressPrinter::Finish() {
  if (!quiet_ && terminal_ && stage_ >= 0) std::fputc('\n', stderr);
  stage_ = -1;
}

}  // namespace sillok::cli
