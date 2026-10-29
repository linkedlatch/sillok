/*
 * sillok: on-device transcription with speaker diarization. C ABI.
 *
 * Conventions
 * - All strings are UTF-8. Strings returned through `char**` are owned by the
 *   caller and released with sillok_free_string().
 * - Structured data (configuration, options, results) is exchanged as JSON
 *   in the schemas described next to each function. Unknown keys in input
 *   JSON are rejected.
 * - Every function that can fail returns a sillok_status; on failure,
 *   sillok_last_error() describes the problem.
 * - Handles are not thread-safe unless noted. sillok_job_cancel() may be
 *   called from any thread at any time.
 */
#ifndef SILLOK_SILLOK_H_
#define SILLOK_SILLOK_H_

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(SILLOK_BUILD_SHARED)
#define SILLOK_API __declspec(dllexport)
#elif defined(SILLOK_USE_SHARED)
#define SILLOK_API __declspec(dllimport)
#else
#define SILLOK_API
#endif
#else
#define SILLOK_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum sillok_status {
  SILLOK_OK = 0,
  SILLOK_INVALID_ARGUMENT = 1, /* bad argument, JSON, option or call order */
  SILLOK_MODEL_ERROR = 2,      /* a model is missing, corrupt or failed */
  SILLOK_CANCELLED = 3,        /* the job was cancelled */
  SILLOK_BUSY = 4,             /* the engine is already running a job */
  SILLOK_INTERNAL_ERROR = 5
} sillok_status;

typedef enum sillok_stage {
  SILLOK_STAGE_ANALYZE = 0,    /* finding speech */
  SILLOK_STAGE_TRANSCRIBE = 1,
  SILLOK_STAGE_DIARIZE = 2
} sillok_stage;

typedef struct sillok_engine sillok_engine;
typedef struct sillok_job sillok_job;

/* Progress of `stage` as a fraction in [0, 1]. Called on the thread running
 * sillok_job_run(); stages arrive in order, each first with 0 as it starts
 * (before its model loads). */
typedef void (*sillok_progress_fn)(void* user_data, sillok_stage stage,
                                   double fraction);

SILLOK_API const char* sillok_version(void);

/* Message of the last failure on the calling thread. Valid until the next
 * sillok call on that thread. */
SILLOK_API const char* sillok_last_error(void);

SILLOK_API void sillok_free_string(char* text);

/* --- Models and devices ------------------------------------------------- */

/* The model files this build needs: name, role, URL, size and SHA-256. The
 * app downloads them into one directory and passes that directory to
 * sillok_engine_create(). */
SILLOK_API sillok_status sillok_model_catalog(char** out_json);

/* State of every catalog file in `models_dir`. Sizes are always checked;
 * hashes only when `verify_hash` is non-zero (reads every file). */
SILLOK_API sillok_status sillok_model_check(const char* models_dir,
                                            int verify_hash, char** out_json);

/* Compute devices this build can use, for the app to offer a choice. */
SILLOK_API sillok_status sillok_devices(char** out_json);

/* --- Engine -------------------------------------------------------------- */

/* `config_json`: {"models_dir": "...", "device": "cpu" | "gpu",
 *                 "gpu_index": 0, "threads": 0, "temp_dir": "..."}
 * Jobs keep their recording in a file in "temp_dir" (default: the system's
 * temporary directory), so memory does not grow with its length. Android has
 * no usable default: pass the app's cache directory there.
 * Models are loaded while a job runs, one stage at a time, and freed
 * afterwards. Jobs keep the engine alive, so the engine may be destroyed
 * before its jobs. */
SILLOK_API sillok_status sillok_engine_create(const char* config_json,
                                              sillok_engine** out_engine);
SILLOK_API void sillok_engine_destroy(sillok_engine* engine);

/* --- Jobs ---------------------------------------------------------------- */

/* One recording. `options_json`: {"language": "auto" | "ko" | "en",
 * "diarize": false, "glossary": ["term", ...]} */
SILLOK_API sillok_status sillok_job_create(sillok_engine* engine,
                                           const char* options_json,
                                           sillok_job** out_job);

/* Appends decoded audio: `frames` frames of `channels` interleaved float
 * samples in [-1, 1] at `sample_rate` Hz (8000-192000). Rate and channel
 * count must stay the same for the whole recording. Transcript times count
 * from the first sample pushed, so push the recording from its start, with
 * encoder priming already removed (decoders such as AVAudioFile and ffmpeg
 * do), for the times to match the original file. */
SILLOK_API sillok_status sillok_job_push_audio(sillok_job* job,
                                               const float* samples,
                                               size_t frames,
                                               int32_t sample_rate,
                                               int32_t channels);

/* --- The job's recording ---------------------------------------------------
 * A job converts the pushed audio to mono at SILLOK_SAMPLE_RATE: the
 * recording it transcribes, which the transcript's times refer to. Sample n
 * lies at n * 1000 / SILLOK_SAMPLE_RATE milliseconds. Apps read it to keep a
 * copy for listening back, so that a segment plays exactly what was
 * transcribed without converting the audio a second time.
 *
 * Samples become available as audio is pushed; the conversion holds back the
 * last few milliseconds until sillok_job_end_audio() (or sillok_job_run())
 * ends the recording. Available samples never change. These functions must
 * not be called while sillok_job_run() is executing on the job. */
#define SILLOK_SAMPLE_RATE 16000

/* Ends the recording: no audio can be pushed afterwards, and every sample is
 * available. sillok_job_run() does this itself; calling it again is allowed. */
SILLOK_API sillok_status sillok_job_end_audio(sillok_job* job);

/* Number of samples available so far. */
SILLOK_API sillok_status sillok_job_audio_length(const sillok_job* job,
                                                 uint64_t* out_samples);

/* Copies up to `count` samples from sample `start` (at most the length) into
 * `samples`, as floats in [-1, 1); `out_read` receives how many. */
SILLOK_API sillok_status sillok_job_read_audio(sillok_job* job, uint64_t start,
                                               float* samples, size_t count,
                                               size_t* out_read);

/* Transcribes the pushed audio and blocks until done. An engine runs one job
 * at a time (SILLOK_BUSY otherwise). A job runs once; no audio can be pushed
 * afterwards. `progress` may be NULL.
 *
 * `out_json` receives the transcript document. Times are milliseconds in the
 * original recording; speakers are ids, named by the app:
 * {"version": 1, "duration_ms": 61000,
 *  "speakers": [{"id": 0, "name": null}, ...],
 *  "segments": [{"start_ms": 0, "end_ms": 2300, "text": "...",
 *                "language": "ko", "speaker": 0,
 *                "words": [{"text": "...", "start_ms": 0, "end_ms": 420,
 *                           "confidence": 0.93, "speaker": 0}, ...]}, ...],
 *  "models": {"transcription": {"name": "...", "device": "...",
 *                               "fell_back_to_cpu": false}, ...}}
 * "speaker" is absent without diarization.
 * Times locate sentences well enough to listen back to them; word times are
 * approximate. To play a segment, start about a second before "start_ms":
 * a segment occasionally starts later than its first word. */
SILLOK_API sillok_status sillok_job_run(sillok_job* job,
                                        sillok_progress_fn progress,
                                        void* user_data, char** out_json);

/* Requests cancellation; sillok_job_run() then returns SILLOK_CANCELLED.
 * Thread-safe; may be called before, during or after the run. */
SILLOK_API void sillok_job_cancel(sillok_job* job);

/* Must not be called while sillok_job_run() is executing on the job. */
SILLOK_API void sillok_job_destroy(sillok_job* job);

/* --- Export -------------------------------------------------------------- */

/* Renders a transcript document (as returned by sillok_job_run(), possibly
 * edited by the app) as "txt", "srt" or "json". `options_json` may be NULL
 * or {"speaker_label": "Speaker {n}"}; speaker names in the document take
 * precedence over the label. */
SILLOK_API sillok_status sillok_export(const char* transcript_json,
                                       const char* format,
                                       const char* options_json,
                                       char** out_text);

#ifdef __cplusplus
}
#endif

#endif /* SILLOK_SILLOK_H_ */
