#include "memory.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <fstream>
#include <string>
#endif

namespace sillok::cli {

namespace {

constexpr double kMegabyte = 1024.0 * 1024.0;

}

MemoryUsage processMemory() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof counters;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof counters)) return {};
    return {counters.PrivateUsage / kMegabyte, counters.PeakPagefileUsage / kMegabyte};
#elif defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return {};
    return {info.phys_footprint / kMegabyte, info.ledger_phys_footprint_peak / kMegabyte};
#else
    MemoryUsage usage;
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) usage.currentMB = std::stod(line.substr(6)) / 1024.0;
        if (line.rfind("VmHWM:", 0) == 0) usage.peakMB = std::stod(line.substr(6)) / 1024.0;
    }
    return usage;
#endif
}

}
