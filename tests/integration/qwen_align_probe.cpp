// Runs Qwen3-ForcedAligner on given words, for scripts that score its times.
// usage: sillok_qwen_align_probe MODELS_DIR AUDIO.f32 WORDS.txt [cpu]
//   AUDIO.f32: 16 kHz mono float32 samples, at most 180 s; WORDS.txt: one
//   aligner unit per line. Prints the time in ms of each slot (two per unit,
//   before FixTimestamps), one per line.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "backends/models.hpp"
#include "backends/qwen/qwen_aligner.hpp"

int main(int argc, char** argv) {
  using namespace sillok;
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s MODELS_DIR AUDIO.f32 WORDS.txt [cpu]\n", argv[0]);
    return 2;
  }
  ListDevices();  // quiets the libraries' logs
  const std::filesystem::path models = argv[1];
  std::ifstream audio_file(argv[2], std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(audio_file)), std::istreambuf_iterator<char>());
  std::vector<float> pcm(bytes.size() / sizeof(float));
  std::memcpy(pcm.data(), bytes.data(), pcm.size() * sizeof(float));
  std::vector<qwen::AlignUnit> units;
  std::ifstream words(argv[3], std::ios::binary);
  for (std::string line; std::getline(words, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) units.push_back({.text = line, .begin = 0, .end = 0});
  }
  const bool cpu = argc > 4 && std::string(argv[4]) == "cpu";
  const auto started = std::chrono::steady_clock::now();
  try {
    qwen::QwenAligner aligner(models / "qwen3-forced-aligner-0.6b-q8_0.gguf",
                              models / "mmproj-qwen3-forced-aligner-0.6b-f16.gguf",
                              ChooseDevice(cpu ? Device::kCpu : Device::kGpu, 0), 8);
    const auto loaded = std::chrono::steady_clock::now();
    const std::vector<int> times = aligner.Align(pcm, units, {}, {});
    const auto done = std::chrono::steady_clock::now();
    for (const int ms : times) std::printf("%d\n", ms);
    std::fprintf(stderr, "%zu units, load %.2f s, align %.2f s\n", units.size(),
                 std::chrono::duration<double>(loaded - started).count(),
                 std::chrono::duration<double>(done - loaded).count());
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
  return 0;
}
