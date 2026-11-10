#include "backends/ggml_devices.hpp"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdlib>
#include <mutex>
#endif

#include <ggml-backend.h>

namespace sillok {
namespace {

// sillok.dll loads Vulkan's loader (vulkan-1.dll, which GPU drivers install)
// only when ggml first calls it (cmake/library.cmake), so that it opens on a
// PC without one. ggml would still call it on listing its devices, and a
// missing DLL there ends the process: ggml is told to skip Vulkan instead,
// which leaves the CPU. Runs before ggml lists its devices, which it does
// once.
void SkipMissingVulkan() {
#ifdef _WIN32
  static std::once_flag once;
  std::call_once(once, [] {
    // Kept loaded, for ggml's calls to find.
    if (LoadLibraryW(L"vulkan-1.dll") == nullptr) _putenv_s("GGML_DISABLE_VULKAN", "1");
  });
#endif
}

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
  SkipMissingVulkan();
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

ggml_backend_device* GgmlGpu(const ComputeDevice& device) {
  if (device.gpu_index < 0) return nullptr;
  int index = 0;
  for (std::size_t i = 0; i < ggml_backend_dev_count(); ++i) {
    const ggml_backend_dev_t candidate = ggml_backend_dev_get(i);
    if (!IsGpu(candidate)) continue;
    if (index++ == device.gpu_index) return candidate;
  }
  return nullptr;
}

std::vector<DeviceInfo> GgmlDevices() {
  SkipMissingVulkan();
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
