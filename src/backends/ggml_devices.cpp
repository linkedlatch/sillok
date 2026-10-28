#include "backends/ggml_devices.hpp"

#include <ggml-backend.h>

namespace sillok {
namespace {

bool IsGpu(ggml_backend_dev_t device) {
  const auto type = ggml_backend_dev_type(device);
  return type == GGML_BACKEND_DEVICE_TYPE_GPU ||
         type == GGML_BACKEND_DEVICE_TYPE_IGPU;
}

const char* TypeName(enum ggml_backend_dev_type type) {
  switch (type) {
    case GGML_BACKEND_DEVICE_TYPE_CPU: return "cpu";
    case GGML_BACKEND_DEVICE_TYPE_GPU: return "gpu";
    case GGML_BACKEND_DEVICE_TYPE_IGPU: return "igpu";
    case GGML_BACKEND_DEVICE_TYPE_ACCEL: return "accel";
    case GGML_BACKEND_DEVICE_TYPE_META: return "meta";
  }
  return "unknown";
}

}  // namespace

ComputeDevice ChooseDevice(Device device, int gpu_index) {
  if (device == Device::kGpu) {
    // whisper.cpp and NeMo-Speech.cpp both count GPU and integrated GPU
    // devices in ggml's order, so this index means the same to both.
    int index = 0;
    for (std::size_t i = 0; i < ggml_backend_dev_count(); ++i) {
      const ggml_backend_dev_t candidate = ggml_backend_dev_get(i);
      if (!IsGpu(candidate)) continue;
      if (index == gpu_index) {
        return {.gpu_index = gpu_index,
                .name = std::string(ggml_backend_dev_name(candidate)) + " (" +
                        ggml_backend_dev_description(candidate) + ")",
                .fell_back_to_cpu = false};
      }
      ++index;
    }
  }
  return {.gpu_index = -1,
          .name = "CPU",
          .fell_back_to_cpu = device == Device::kGpu};
}

std::vector<DeviceInfo> GgmlDevices() {
  std::vector<DeviceInfo> devices;
  for (std::size_t i = 0; i < ggml_backend_dev_count(); ++i) {
    const ggml_backend_dev_t device = ggml_backend_dev_get(i);
    std::size_t free = 0;
    std::size_t total = 0;
    ggml_backend_dev_memory(device, &free, &total);
    devices.push_back({.name = ggml_backend_dev_name(device),
                       .description = ggml_backend_dev_description(device),
                       .type = TypeName(ggml_backend_dev_type(device)),
                       .memory_total = total,
                       .memory_free = free});
  }
  return devices;
}

}  // namespace sillok
