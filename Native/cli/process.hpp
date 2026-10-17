#pragma once

#include <string>
#include <vector>

namespace sillok::cli {

constexpr int kCommandNotFound = -1;

int runProcess(const std::vector<std::string>& arguments);

}
