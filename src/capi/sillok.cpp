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
#include <span>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "backends/models.hpp"
#include "capi/json_io.hpp"
#include "core/audio.hpp"
#include "core/cancel.hpp"
#include "core/files.hpp"
#include "core/pipeline.hpp"
#include "media/audio_file.hpp"

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
  } catch (const MediaError& error) {
    return Fail(SILLOK_MEDIA_ERROR, error.what());
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
  std::unique_ptr<ModelProvider> models;
  std::filesystem::path temp_dir;
  std::atomic<bool> busy = false;
};

}  // namespace
}  // namespace sillok

struct sillok_engine {
  std::shared_ptr<sillok::EngineState> state;
};

static_assert(SILLOK_SAMPLE_RATE == sillok::kSampleRate);

struct sillok_job {
  sillok_job(std::shared_ptr<sillok::EngineState> engine_state,
             sillok::PipelineOptions pipeline_options)
      : engine(std::move(engine_state)),
        options(std::move(pipeline_options)),
        audio(engine->temp_dir) {}

  std::shared_ptr<sillok::EngineState> engine;
  sillok::PipelineOptions options;
  sillok::AudioBuffer audio;  // kept in a temporary file
  sillok::StopSource stop;
  bool ran = false;
  sillok_segments_fn on_segments = nullptr;
  void* segments_user_data = nullptr;
};

namespace sillok {
namespace {

// Thread running a job.
struct RunThread {
  std::thread::id id;
#ifdef _WIN32
  // When a DLL's exit handlers run, ExitProcess has already ended the other
  // threads, so their runs never finish; the handle tells that apart.
  HANDLE handle = nullptr;
#endif
};

bool Ended(const RunThread& thread) {
#ifdef _WIN32
  return thread.handle != nullptr && WaitForSingleObject(thread.handle, 0) == WAIT_OBJECT_0;
#else
  static_cast<void>(thread);
  return false;
#endif
}

// Jobs running right now, so that process exit can stop them before the
// static objects of the model libraries are destroyed under them.
struct ActiveRuns {
  std::mutex mutex;
  std::condition_variable idle;
  std::map<sillok_job*, RunThread> jobs;
  bool exiting = false;
};

ActiveRuns& Runs() {
  static ActiveRuns* const runs = new ActiveRuns;  // never destroyed
  return *runs;
}

// atexit handler. Cancels running jobs and waits for them, except a job on
// the exiting thread itself, which cannot finish. whisper.cpp checks for
// cancellation only between encoder and decoder passes, so stopping can take
// one encoder pass: a second or less on a GPU, tens of seconds on a slow CPU.
// The bound only guards against a run that never returns.
void StopRunsAtExit() {
  ActiveRuns& runs = Runs();
  std::unique_lock lock(runs.mutex);
  if (runs.exiting) return;  // a later registration already ran
  runs.exiting = true;
  const std::thread::id self = std::this_thread::get_id();
  for (const auto& [job, thread] : runs.jobs) job->stop.RequestStop();
  const auto stopped = [&] {
    return std::all_of(runs.jobs.begin(), runs.jobs.end(), [&](const auto& run) {
      return run.second.id == self || Ended(run.second);
    });
  };
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  // Polls as well, since an ended thread notifies nobody.
  while (!stopped() && std::chrono::steady_clock::now() < deadline) {
    runs.idle.wait_for(lock, std::chrono::milliseconds(100));
  }
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
    RunThread& thread = runs.jobs[job_];
    thread.id = std::this_thread::get_id();
#ifdef _WIN32
    thread.handle = OpenThread(SYNCHRONIZE, FALSE, GetCurrentThreadId());
#endif
  }
  ~RunScope() {
    ActiveRuns& runs = Runs();
    const std::lock_guard lock(runs.mutex);
#ifdef _WIN32
    if (runs.jobs[job_].handle != nullptr) CloseHandle(runs.jobs[job_].handle);
#endif
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

sillok_status sillok_engine_models(const char* transcription_model,
                                   char** out_json) {
  return Guard([&] {
    RequireArgument(out_json != nullptr, "out_json is NULL");
    *out_json = sillok::CopyString(sillok::CatalogToJson(sillok::EngineModels(
        sillok::ModelCatalog(),
        transcription_model != nullptr ? transcription_model : "")));
    return SILLOK_OK;
  });
}

sillok_status sillok_model_check(const char* models_dir, int verify_hash,
                                 char** out_json) {
  return Guard([&] {
    RequireArgument(models_dir != nullptr && out_json != nullptr,
                    "models_dir and out_json are required");
    const std::filesystem::path dir = sillok::PathFromUtf8(models_dir);
    const std::vector<sillok::CatalogModel> catalog = sillok::ModelCatalog();
    std::vector<sillok::CatalogStatus> statuses;
    for (const sillok::CatalogModel& model : catalog) {
      for (const sillok::CatalogFile& file : model.files) {
        const std::filesystem::path path = dir / sillok::PathFromUtf8(file.file);
        std::string status;
        if (file.zip && std::filesystem::is_directory(path.parent_path() / path.stem())) {
          status = "ok";
        } else {
          switch (sillok::CheckFile(path, file.size, file.sha256, verify_hash != 0)) {
            case sillok::FileCheck::kOk: status = file.zip ? "extract" : "ok"; break;
            case sillok::FileCheck::kMissing: status = "missing"; break;
            case sillok::FileCheck::kWrongSize: status = "wrong_size"; break;
            case sillok::FileCheck::kWrongHash: status = "wrong_hash"; break;
          }
        }
        statuses.push_back({&model, &file, std::move(status)});
      }
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
    const sillok::EngineSettings settings = sillok::ParseEngineConfig(config_json);
    auto state = std::make_shared<sillok::EngineState>();
    state->temp_dir = settings.temp_dir.empty()
                          ? std::filesystem::temp_directory_path()
                          : settings.temp_dir;
    if (!std::filesystem::is_directory(state->temp_dir)) {
      throw std::invalid_argument("temp_dir is not a directory: " +
                                  sillok::PathToUtf8(state->temp_dir));
    }
    state->models = sillok::OpenModels(settings.models);
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
    auto job = std::make_unique<sillok_job>(
        engine->state, sillok::ParseJobOptions(options_json ? options_json : ""));
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
    RequireArgument(!job->audio.finished(),
                    "audio cannot be added after sillok_job_end_audio");
    job->audio.Push({samples, frames * static_cast<size_t>(channels)},
                    sample_rate, channels);
    return SILLOK_OK;
  });
}

sillok_status sillok_job_push_file(sillok_job* job, const char* path,
                                   sillok_read_progress_fn progress,
                                   void* user_data) {
  return Guard([&] {
    RequireArgument(job != nullptr && path != nullptr, "job and path are required");
    RequireArgument(!job->ran, "audio cannot be added after the job ran");
    RequireArgument(!job->audio.finished(),
                    "audio cannot be added after sillok_job_end_audio");
    sillok::DecodeAudioFile(
        sillok::PathFromUtf8(path), job->audio,
        [&](double fraction) {
          if (progress != nullptr) progress(user_data, fraction);
        },
        job->stop.Token());
    return SILLOK_OK;
  });
}

sillok_status sillok_job_end_audio(sillok_job* job) {
  return Guard([&] {
    RequireArgument(job != nullptr, "job is NULL");
    job->audio.Finish();
    return SILLOK_OK;
  });
}

sillok_status sillok_job_audio_length(const sillok_job* job, uint64_t* out_samples) {
  return Guard([&] {
    RequireArgument(job != nullptr && out_samples != nullptr,
                    "job and out_samples are required");
    *out_samples = static_cast<uint64_t>(job->audio.size());
    return SILLOK_OK;
  });
}

sillok_status sillok_job_read_audio(sillok_job* job, uint64_t start,
                                    float* samples, size_t count,
                                    size_t* out_read) {
  return Guard([&] {
    RequireArgument(job != nullptr && (samples != nullptr || count == 0) &&
                        out_read != nullptr,
                    "job, samples and out_read are required");
    *out_read = 0;
    const auto length = static_cast<uint64_t>(job->audio.size());
    RequireArgument(start <= length, "start is past the end of the recording");
    const uint64_t end = start + std::min<uint64_t>(count, length - start);
    const std::vector<float> read = job->audio.Read(
        {static_cast<sillok::SampleIndex>(start), static_cast<sillok::SampleIndex>(end)});
    std::copy(read.begin(), read.end(), samples);
    *out_read = read.size();
    return SILLOK_OK;
  });
}

sillok_status sillok_job_on_segments(sillok_job* job, sillok_segments_fn callback,
                                     void* user_data) {
  return Guard([&] {
    RequireArgument(job != nullptr, "job is NULL");
    RequireArgument(!job->ran, "the job already ran");
    job->on_segments = callback;
    job->segments_user_data = user_data;
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
    sillok::ThrowIfStopped(job->stop.Token());

    job->audio.Finish();
    sillok::ExitSafeModels models(*job->engine->models);
    const sillok::Transcript transcript = sillok::RunPipeline(
        job->audio, job->options, models,
        [&](sillok::Stage stage, double fraction) {
          if (progress) progress(user_data, static_cast<sillok_stage>(stage), fraction);
        },
        job->stop.Token(),
        [&](std::span<const sillok::Segment> segments) {
          if (job->on_segments == nullptr) return;
          const std::string json = sillok::SegmentsToJson(segments);
          job->on_segments(job->segments_user_data, json.c_str());
        });
    *out_json = sillok::CopyString(sillok::TranscriptToJson(transcript));
    return SILLOK_OK;
  });
}

void sillok_job_cancel(sillok_job* job) {
  if (job != nullptr) job->stop.RequestStop();
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
