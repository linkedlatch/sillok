#ifndef SILLOK_APPS_CLI_PROGRESS_HPP_
#define SILLOK_APPS_CLI_PROGRESS_HPP_

#include "sillok/sillok.h"

namespace sillok::cli {

// Progress on stderr: one rewritten line on a terminal; otherwise (logs,
// pipes) a plain line per stage start and every 10%.
class ProgressPrinter {
 public:
  explicit ProgressPrinter(bool quiet);

  void Update(sillok_stage stage, double fraction);
  // Ends the current line.
  void Finish();

 private:
  bool quiet_;
  bool terminal_;
  int stage_ = -1;
  int percent_ = -1;
};

// Whether standard error is a terminal.
bool StderrIsTerminal();

}  // namespace sillok::cli

#endif  // SILLOK_APPS_CLI_PROGRESS_HPP_
