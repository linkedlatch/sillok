// Measures cancellation and process exit during a real run, through the C ABI.
//
//   sillok_probe cancel MODELS DEVICE WAV DELAY_MS
//     Cancels DELAY_MS after the run starts; prints how long the run took to
//     return afterwards.
//   sillok_probe exit MODELS DEVICE WAV DELAY_MS
//     Calls std::exit(0) DELAY_MS after the run starts, while it is still
//     running on another thread. The process must end normally.
// WAV must be 16 kHz mono.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "sillok/sillok.h"
#include "wav_reader.hpp"

namespace {

using Clock = std::chrono::steady_clock;

sillok_job* StartJob(const char* models, const char* device, const char* wav) {
  const nlohmann::json config = {{"models_dir", models}, {"device", device}};
  sillok_engine* engine = nullptr;
  if (sillok_engine_create(config.dump().c_str(), &engine) != SILLOK_OK) {
    std::fprintf(stderr, "engine: %s\n", sillok_last_error());
    std::exit(2);
  }
  sillok_job* job = nullptr;
  sillok_job_create(engine, R"({"language": "auto", "diarize": true})", &job);
  sillok_engine_destroy(engine);  // the job keeps it alive
  std::FILE* file = std::fopen(wav, "rb");
  if (file == nullptr) std::exit(2);
  sillok::cli::WavReader reader(file);
  while (true) {
    const auto samples = reader.Read(16000);
    if (samples.empty()) break;
    sillok_job_push_audio(job, samples.data(), samples.size(), 16000, 1);
  }
  std::fclose(file);
  return job;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6) {
    std::fprintf(stderr, "usage: sillok_probe cancel|exit MODELS DEVICE WAV DELAY_MS\n");
    return 2;
  }
  const std::string mode = argv[1];
  sillok_job* job = StartJob(argv[2], argv[3], argv[4]);
  const auto delay = std::chrono::milliseconds(std::stol(argv[5]));

  sillok_status status = SILLOK_OK;
  Clock::time_point returned;
  std::thread runner([&] {
    char* out = nullptr;
    status = sillok_job_run(job, nullptr, nullptr, &out);
    returned = Clock::now();
    sillok_free_string(out);
  });
  std::this_thread::sleep_for(delay);

  if (mode == "exit") {
    std::printf("{\"exit_after_ms\": %ld}\n", static_cast<long>(delay.count()));
    std::fflush(stdout);
    std::exit(0);  // the runner is still inside sillok_job_run
  }
  const Clock::time_point cancelled = Clock::now();
  sillok_job_cancel(job);
  runner.join();
  const auto latency =
      std::chrono::duration_cast<std::chrono::milliseconds>(returned - cancelled).count();
  std::printf("{\"status\": %d, \"cancel_to_return_ms\": %lld}\n", static_cast<int>(status),
              static_cast<long long>(latency));
  sillok_job_destroy(job);
  return 0;
}
