#include "sillok/sillok.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <stop_token>
#include <string>
#include <thread>

#include "backends/models.hpp"
#include "capi/json_io.hpp"
#include "core/cancel.hpp"
#include "core/pipeline.hpp"

namespace sillok {
namespace {

thread_local std::string last_error;

sillok_status Fail(sillok_status status, std::string message) {
  last_error = std::move(message);
  return status;
}

// Runs `body`, turning exceptions into status codes.
template <typename Body>
sillok_status Guard(Body&& body) noexcept {
  try {
    return body();
  } catch (const Cancelled&) {
    return Fail(SILLOK_CANCELLED, "cancelled");
  } catch (const ModelError& error) {
    return Fail(SILLOK_MODEL_ERROR, error.what());
  } catch (const std::invalid_argument& error) {
    return Fail(SILLOK_INVALID_ARGUMENT, error.what());
  } catch (const std::logic_error& error) {
    return Fail(SILLOK_INVALID_ARGUMENT, error.what());
  } catch (const std::bad_alloc&) {
    return Fail(SILLOK_INTERNAL_ERROR, "out of memory");
  } catch (const std::exception& error) {
    return Fail(SILLOK_INTERNAL_ERROR, error.what());
  } catch (...) {
    return Fail(SILLOK_INTERNAL_ERROR, "unknown error");
  }
}

char* CopyString(std::string_view text) {
  auto* copy = static_cast<char*>(std::malloc(text.size() + 1));
  if (copy == nullptr) throw std::bad_alloc();
  std::memcpy(copy, text.data(), text.size());
  copy[text.size()] = '\0';
  return copy;
}

void RequireArgument(bool ok, const char* message) {
  if (!ok) throw std::invalid_argument(message);
}

struct EngineState {
  EngineConfig config;
  std::unique_ptr<ModelProvider> models;
  std::atomic<bool> busy = false;
};

}  // namespace
}  // namespace sillok

struct sillok_engine {
  std::shared_ptr<sillok::EngineState> state;
};

struct sillok_job {
  std::shared_ptr<sillok::EngineState> engine;
  sillok::PipelineOptions options;
  sillok::AudioBuffer audio;
  std::stop_source stop;
  bool ran = false;
};

namespace sillok {
namespace {

// Jobs running right now, so that process exit can stop them before the
// static objects of the model libraries are destroyed under them.
struct ActiveRuns {
  std::mutex mutex;
  std::condition_variable idle;
  std::map<sillok_job*, std::thread::id> jobs;
  bool exiting = false;
};

ActiveRuns& Runs() {
  static ActiveRuns* const runs = new ActiveRuns;  // never destroyed
  return *runs;
}

// atexit handler. Cancels running jobs and waits for them, except a job on
// the exiting thread itself, which cannot finish.
void StopRunsAtExit() {
  ActiveRuns& runs = Runs();
  std::unique_lock lock(runs.mutex);
  if (runs.exiting) return;  // a later registration already ran
  runs.exiting = true;
  const std::thread::id self = std::this_thread::get_id();
  for (const auto& [job, thread] : runs.jobs) job->stop.request_stop();
  runs.idle.wait_for(lock, std::chrono::seconds(10), [&] {
    return std::all_of(runs.jobs.begin(), runs.jobs.end(),
                       [&](const auto& run) { return run.second == self; });
  });
}

// atexit handlers run in reverse order of registration and interleave with
// the destruction of static objects. Registering after the first load of
// each model kind makes the handler run before the statics that loading
// created (backend registries, GPU devices) are destroyed.
void RegisterExitHandlerOnce(std::atomic<bool>& registered) {
  if (!registered.exchange(true)) std::atexit(StopRunsAtExit);
}

// Forwards to the engine's models, registering the exit handler after the
// first load of each kind.
class ExitSafeModels : public ModelProvider {
 public:
  explicit ExitSafeModels(ModelProvider& models) : models_(models) {}

  std::unique_ptr<VoiceDetector> OpenVoiceDetector() override {
    auto model = models_.OpenVoiceDetector();
    RegisterExitHandlerOnce(voice_detector_loaded);
    return model;
  }
  std::unique_ptr<Transcriber> OpenTranscriber() override {
    auto model = models_.OpenTranscriber();
    RegisterExitHandlerOnce(transcriber_loaded);
    return model;
  }
  std::unique_ptr<Diarizer> OpenDiarizer() override {
    auto model = models_.OpenDiarizer();
    RegisterExitHandlerOnce(diarizer_loaded);
    return model;
  }

  static inline std::atomic<bool> engine_created = false;
  static inline std::atomic<bool> voice_detector_loaded = false;
  static inline std::atomic<bool> transcriber_loaded = false;
  static inline std::atomic<bool> diarizer_loaded = false;

 private:
  ModelProvider& models_;
};

// Marks a job as running for the duration of a scope.
class RunScope {
 public:
  explicit RunScope(sillok_job* job) : job_(job) {
    ActiveRuns& runs = Runs();
    const std::lock_guard lock(runs.mutex);
    if (runs.exiting) throw Cancelled();
    runs.jobs[job_] = std::this_thread::get_id();
  }
  ~RunScope() {
    ActiveRuns& runs = Runs();
    const std::lock_guard lock(runs.mutex);
    runs.jobs.erase(job_);
    runs.idle.notify_all();
  }
  RunScope(const RunScope&) = delete;
  RunScope& operator=(const RunScope&) = delete;

 private:
  sillok_job* job_;
};

// Holds the engine's single run slot.
class BusyScope {
 public:
  explicit BusyScope(std::atomic<bool>& busy) : busy_(busy) {}
  ~BusyScope() { busy_ = false; }
  BusyScope(const BusyScope&) = delete;
  BusyScope& operator=(const BusyScope&) = delete;

 private:
  std::atomic<bool>& busy_;
};

}  // namespace
}  // namespace sillok

using sillok::Guard;
using sillok::RequireArgument;

extern "C" {

const char* sillok_version(void) { return "sillok " SILLOK_VERSION; }

const char* sillok_last_error(void) { return sillok::last_error.c_str(); }

void sillok_free_string(char* text) { std::free(text); }

sillok_status sillok_model_catalog(char** out_json) {
  return Guard([&] {
    RequireArgument(out_json != nullptr, "out_json is NULL");
    *out_json = sillok::CopyString(sillok::CatalogToJson(sillok::ModelCatalog()));
    return SILLOK_OK;
  });
}

sillok_status sillok_model_check(const char* models_dir, int verify_hash,
                                 char** out_json) {
  return Guard([&] {
    RequireArgument(models_dir != nullptr && out_json != nullptr,
                    "models_dir and out_json are required");
    const std::filesystem::path dir = sillok::PathFromUtf8(models_dir);
    const std::vector<sillok::CatalogEntry> catalog = sillok::ModelCatalog();
    std::vector<sillok::CatalogStatus> statuses;
    for (const sillok::CatalogEntry& entry : catalog) {
      const std::filesystem::path path = dir / sillok::PathFromUtf8(entry.file);
      std::string status;
      if (entry.zip && std::filesystem::is_directory(path.parent_path() / path.stem())) {
        status = "ok";
      } else {
        switch (sillok::CheckFile(path, entry.size, entry.sha256, verify_hash != 0)) {
          case sillok::FileCheck::kOk: status = entry.zip ? "extract" : "ok"; break;
          case sillok::FileCheck::kMissing: status = "missing"; break;
          case sillok::FileCheck::kWrongSize: status = "wrong_size"; break;
          case sillok::FileCheck::kWrongHash: status = "wrong_hash"; break;
        }
      }
      statuses.push_back({&entry, std::move(status)});
    }
    *out_json = sillok::CopyString(sillok::CatalogStatusToJson(statuses));
    return SILLOK_OK;
  });
}

sillok_status sillok_devices(char** out_json) {
  return Guard([&] {
    RequireArgument(out_json != nullptr, "out_json is NULL");
    *out_json = sillok::CopyString(sillok::DevicesToJson(sillok::ListDevices()));
    return SILLOK_OK;
  });
}

sillok_status sillok_engine_create(const char* config_json,
                                   sillok_engine** out_engine) {
  return Guard([&] {
    RequireArgument(config_json != nullptr && out_engine != nullptr,
                    "config_json and out_engine are required");
    *out_engine = nullptr;
    auto state = std::make_shared<sillok::EngineState>();
    state->config = sillok::ParseEngineConfig(config_json);
    state->models = sillok::OpenModels(state->config);
    sillok::RegisterExitHandlerOnce(sillok::ExitSafeModels::engine_created);
    *out_engine = new sillok_engine{std::move(state)};
    return SILLOK_OK;
  });
}

void sillok_engine_destroy(sillok_engine* engine) { delete engine; }

sillok_status sillok_job_create(sillok_engine* engine, const char* options_json,
                                sillok_job** out_job) {
  return Guard([&] {
    RequireArgument(engine != nullptr && out_job != nullptr,
                    "engine and out_job are required");
    *out_job = nullptr;
    auto job = std::make_unique<sillok_job>();
    job->engine = engine->state;
    job->options = sillok::ParseJobOptions(options_json ? options_json : "");
    *out_job = job.release();
    return SILLOK_OK;
  });
}

sillok_status sillok_job_push_audio(sillok_job* job, const float* samples,
                                    size_t frames, int32_t sample_rate,
                                    int32_t channels) {
  return Guard([&] {
    RequireArgument(job != nullptr && (samples != nullptr || frames == 0),
                    "job and samples are required");
    RequireArgument(channels > 0, "channels must be positive");
    RequireArgument(!job->ran, "audio cannot be added after the job ran");
    job->audio.Push({samples, frames * static_cast<size_t>(channels)},
                    sample_rate, channels);
    return SILLOK_OK;
  });
}

sillok_status sillok_job_run(sillok_job* job, sillok_progress_fn progress,
                             void* user_data, char** out_json) {
  return Guard([&] {
    RequireArgument(job != nullptr && out_json != nullptr,
                    "job and out_json are required");
    *out_json = nullptr;
    RequireArgument(!job->ran, "a job runs only once");
    bool idle = false;
    if (!job->engine->busy.compare_exchange_strong(idle, true)) {
      return sillok::Fail(SILLOK_BUSY, "the engine is running another job");
    }
    const sillok::BusyScope busy(job->engine->busy);
    const sillok::RunScope running(job);
    job->ran = true;
    sillok::ThrowIfStopped(job->stop.get_token());

    job->audio.Finish();
    sillok::ExitSafeModels models(*job->engine->models);
    const sillok::Transcript transcript = sillok::RunPipeline(
        job->audio, job->options, models,
        [&](sillok::Stage stage, double fraction) {
          if (progress) progress(user_data, static_cast<sillok_stage>(stage), fraction);
        },
        job->stop.get_token());
    *out_json = sillok::CopyString(sillok::TranscriptToJson(transcript));
    return SILLOK_OK;
  });
}

void sillok_job_cancel(sillok_job* job) {
  if (job != nullptr) job->stop.request_stop();
}

void sillok_job_destroy(sillok_job* job) { delete job; }

sillok_status sillok_export(const char* transcript_json, const char* format,
                            const char* options_json, char** out_text) {
  return Guard([&] {
    RequireArgument(transcript_json != nullptr && format != nullptr &&
                        out_text != nullptr,
                    "transcript_json, format and out_text are required");
    const sillok::Transcript transcript =
        sillok::TranscriptFromJson(transcript_json);
    const sillok::ExportOptions options =
        sillok::ParseExportOptions(options_json ? options_json : "");
    const std::string_view kind = format;
    std::string text;
    if (kind == "txt") {
      text = sillok::ToText(transcript, options);
    } else if (kind == "srt") {
      text = sillok::ToSrt(transcript, options);
    } else if (kind == "json") {
      text = sillok::TranscriptToJson(transcript);
    } else {
      throw std::invalid_argument("format must be \"txt\", \"srt\" or \"json\"");
    }
    *out_text = sillok::CopyString(text);
    return SILLOK_OK;
  });
}

}  // extern "C"
