#ifndef SILLOK_BACKENDS_GGML_DEVICES_HPP_
#define SILLOK_BACKENDS_GGML_DEVICES_HPP_

#include <string>
#include <vector>

#include "backends/models.hpp"

namespace sillok {

// Device a ggml-based model runs on.
struct ComputeDevice {
  int gpu_index = -1;  // among ggml's GPU devices; -1 for the CPU
  std::string name;    // for ModelInfo
  bool fell_back_to_cpu = false;
};

// The requested device, or the CPU when no GPU with that index exists.
ComputeDevice ChooseDevice(Device device, int gpu_index);

// All devices ggml was built with and found.
std::vector<DeviceInfo> GgmlDevices();

}  // namespace sillok

#endif  // SILLOK_BACKENDS_GGML_DEVICES_HPP_
