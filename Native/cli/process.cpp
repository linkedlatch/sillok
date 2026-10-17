#include "process.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace sillok::cli {

#ifdef _WIN32

namespace {

std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::wstring quoted(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) return argument;
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        result.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        result += c;
    }
    result.append(backslashes * 2, L'\\');
    return result + L"\"";
}

}

int runProcess(const std::vector<std::string>& arguments) {
    std::wstring commandLine;
    for (const auto& argument : arguments) {
        if (!commandLine.empty()) commandLine += L' ';
        commandLine += quoted(wide(argument));
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) {
        return kCommandNotFound;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}

#else

int runProcess(const std::vector<std::string>& arguments) {
    std::vector<char*> argv;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0) return kCommandNotFound;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return 1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

#endif

}
