#ifndef SILLOK_APPS_CLI_PROCESS_HPP_
#define SILLOK_APPS_CLI_PROCESS_HPP_

#include <cstdio>
#include <string>
#include <vector>

namespace sillok::cli {

// A child process started without a shell; `args[0]` is looked up on PATH.
// Arguments are UTF-8.
class Process {
 public:
  // With `capture_output`, the child's standard output is readable through
  // output(); otherwise the child shares this process's output. Throws
  // std::runtime_error if the program cannot be started.
  Process(const std::vector<std::string>& args, bool capture_output);
  // Stops a child that is still running.
  ~Process();
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;

  std::FILE* output() const { return output_; }
  // Waits for the child; returns its exit code, or -1 if it did not exit
  // normally.
  int Wait();

 private:
  std::FILE* output_ = nullptr;
  bool waited_ = false;
  int exit_code_ = -1;
#ifdef _WIN32
  void* handle_ = nullptr;
#else
  int pid_ = -1;
#endif
};

}  // namespace sillok::cli

#endif  // SILLOK_APPS_CLI_PROCESS_HPP_
