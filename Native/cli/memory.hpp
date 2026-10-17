#pragma once

namespace sillok::cli {

struct MemoryUsage {
    double currentMB = 0;
    double peakMB = 0;
};

MemoryUsage processMemory();

}
