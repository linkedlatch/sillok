#include "process.hpp"

#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace sillok::cli {

#ifdef _WIN32
namespace {

std::wstring Utf8ToWide(const std::string& text) {
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size > 0 ? size - 1 : 0), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
  return out;
}

// Quotes one argument by the rules CommandLineToArgvW reverses.
std::wstring Quote(const std::wstring& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
  std::wstring out = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
    backslashes = 0;
    out.push_back(c);
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

}  // namespace

Process::Process(const std::vector<std::string>& args, bool capture_output) {
  std::wstring command;
  for (const std::string& arg : args) {
    if (!command.empty()) command.push_back(L' ');
    command += Quote(Utf8ToWide(arg));
  }
  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE read_end = nullptr;
  HANDLE write_end = nullptr;
  STARTUPINFOW startup{};
  startup.cb = sizeof startup;
  if (capture_output) {
    if (!CreatePipe(&read_end, &write_end, &inherit, 0)) {
      throw std::runtime_error("cannot create a pipe");
    }
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = write_end;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  }
  PROCESS_INFORMATION info{};
  const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                                      0, nullptr, nullptr, &startup, &info);
  if (write_end) CloseHandle(write_end);
  if (!started) {
    if (read_end) CloseHandle(read_end);
    throw std::runtime_error("cannot run " + args.front());
  }
  CloseHandle(info.hThread);
  handle_ = info.hProcess;
  if (capture_output) {
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(read_end), _O_RDONLY | _O_BINARY);
    output_ = _fdopen(fd, "rb");
  }
}

Process::~Process() {
  if (output_) std::fclose(output_);
  if (!waited_) TerminateProcess(handle_, 1);
  Wait();
  CloseHandle(handle_);
}

int Process::Wait() {
  if (!waited_) {
    WaitForSingleObject(handle_, INFINITE);
    DWORD code = 0;
    exit_code_ = GetExitCodeProcess(handle_, &code) ? static_cast<int>(code) : -1;
    waited_ = true;
  }
  return exit_code_;
}

#else

Process::Process(const std::vector<std::string>& args, bool capture_output) {
  std::vector<char*> argv;
  for (const std::string& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
  argv.push_back(nullptr);

  int pipe_ends[2] = {-1, -1};
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  if (capture_output) {
    if (pipe(pipe_ends) != 0) throw std::runtime_error("cannot create a pipe");
    posix_spawn_file_actions_addclose(&actions, pipe_ends[0]);
    posix_spawn_file_actions_adddup2(&actions, pipe_ends[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipe_ends[1]);
  }
  pid_t pid = -1;
  const int error = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (capture_output) close(pipe_ends[1]);
  if (error != 0) {
    if (capture_output) close(pipe_ends[0]);
    throw std::runtime_error("cannot run " + args.front() + ": " + std::strerror(error));
  }
  pid_ = pid;
  if (capture_output) output_ = fdopen(pipe_ends[0], "rb");
}

Process::~Process() {
  if (output_) std::fclose(output_);
  if (!waited_) kill(pid_, SIGTERM);
  Wait();
}

int Process::Wait() {
  if (!waited_) {
    int status = 0;
    while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
    }
    exit_code_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    waited_ = true;
  }
  return exit_code_;
}

#endif

}  // namespace sillok::cli
