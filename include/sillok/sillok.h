/*
 * sillok: on-device transcription with speaker diarization. C ABI.
 *
 * Conventions
 * - All strings are UTF-8. Strings returned through `char**` are owned by the
 *   caller and released with sillok_free_string().
 * - Structured data (configuration, options, results) is exchanged as JSON
 *   in the schemas described next to each function. Unknown keys in input
 *   JSON are rejected, except in a transcript document (sillok_export), where
 *   they are ignored so that documents saved by older versions still read.
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
  SILLOK_INTERNAL_ERROR = 5,   /* a bug or an unexpected failure inside the library */
  SILLOK_MEDIA_ERROR = 6       /* a file cannot be opened or has no audio to decode */
} sillok_status;

typedef enum sillok_stage {
  SILLOK_STAGE_ANALYZE = 0,    /* finding speech */
  SILLOK_STAGE_TRANSCRIBE = 1,
  SILLOK_STAGE_ALIGN = 2,      /* word times, for a model with an aligner */
  SILLOK_STAGE_DIARIZE = 3
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

/* The models this build can use, each with the files to download into one
 * directory, which the app passes to sillok_engine_create():
 * [{"id": "whisper-large-v3-turbo-q5_0", "role": "transcription",
 *   "backend": "whisper", "family": "large-v3-turbo", "quantization": "q5_0",
 *   "recommended": true,
 *   "files": [{"file": "...", "url": "...", "size": 574041195,
 *              "sha256": "...", "zip": false}, ...],
 *   "languages": ["ko", "en", "ja", ...], "aligner": false,
 *   "memory": 919220000}, ...]
 * Roles are "voice_detection", "transcription" and "diarization", and
 * "backend" names what runs a model ("silero", "whisper", "qwen",
 * "sortformer"); an engine
 * needs one model of each, the recommended one unless the engine config
 * names another transcription model. A "zip" file is an optional
 * accelerator (the Core ML encoder on Apple) that the app extracts into the
 * directory. Files may be shared between models. A transcription model lists
 * the "languages" a job's "language" may name (ISO 639-1 where there is a
 * code, else "haw" for Hawaiian and "yue" for Cantonese): all of Whisper's
 * for a Whisper model, the eleven Qwen3-ForcedAligner aligns for Qwen;
 * "auto" picks among them alone. "aligner" says whether its word times come
 * from an aligner of its own, which a job runs in the align stage. Its
 * "memory" is the bytes it takes while it runs, worked out from its buffers,
 * CPU or GPU alike: the free memory a job with it wants. */
SILLOK_API sillok_status sillok_model_catalog(char** out_json);

/* The catalog entries an engine uses, one per role, for the app to download
 * and check: each role's recommended model, except `transcription_model`
 * (a catalog id; NULL or "" for the recommended one), as in the engine
 * config. SILLOK_INVALID_ARGUMENT when no transcription model has that
 * id. */
SILLOK_API sillok_status sillok_engine_models(const char* transcription_model,
                                              char** out_json);

/* State of every catalog file in `models_dir`:
 * [{"model": "...", "role": "...", "file": "...", "status": "ok"}, ...] with
 * "ok", "missing", "wrong_size", "wrong_hash" or, for archives, "extract".
 * Sizes are always checked; hashes only when `verify_hash` is non-zero
 * (reads every file). */
SILLOK_API sillok_status sillok_model_check(const char* models_dir,
                                            int verify_hash, char** out_json);

/* Compute devices this build can use, for the app to offer a choice. */
SILLOK_API sillok_status sillok_devices(char** out_json);

/* --- Engine -------------------------------------------------------------- */

/* `config_json`: {"models_dir": "...", "device": "cpu" | "gpu",
 *                 "gpu_index": 0, "threads": 0, "npu": false,
 *                 "transcription_model": "<catalog id>"}
 * Without "transcription_model" the recommended one is used. Only the
 * engine's models need to be in "models_dir".
 * "npu" runs the transcription encoder on the NPU when the model's
 * accelerator file is in "models_dir" (the Core ML encoder on Apple's Neural
 * Engine), with either device; without that file the encoder runs on
 * "device". With "device": "cpu" and "npu": false, neither a GPU nor an NPU
 * is used.
 * Models are loaded while a job runs, one stage at a time, and freed
 * afterwards. Jobs keep the engine alive, so the engine may be destroyed
 * before its jobs. */
SILLOK_API sillok_status sillok_engine_create(const char* config_json,
                                              sillok_engine** out_engine);
SILLOK_API void sillok_engine_destroy(sillok_engine* engine);

/* --- Jobs ---------------------------------------------------------------- */

/* One recording. `options_json`: {"language": "auto" | "<code>",
 * "diarize": false, "glossary": ["term", ...], "even_loudness": true,
 * "checkpoint": "<path>"}
 * "language": "auto" tells the language of each piece the model hears at
 * once (speech joined up to 30 s, or less for a model that takes less),
 * among the model's "languages", so a recording may switch language between
 * pieces; a code
 * (one of the transcription model's "languages" in the catalog)
 * transcribes everything in that language. SILLOK_INVALID_ARGUMENT for a
 * language the model does not know. Each segment of the transcript carries
 * the language it was transcribed in.
 * "glossary" lists terms the recording may contain (names, jargon), which
 * the model is given with every piece it hears. The model holds the terms,
 * the piece and its text in a fixed number of tokens, and would otherwise
 * drop terms or stop short, so sillok_job_run() refuses a glossary that does
 * not fit (SILLOK_INVALID_ARGUMENT, with its length in tokens and the
 * limit) before transcribing. Whisper takes 223 tokens, about 50 English or
 * 35 Korean terms; Qwen3-ASR about 7000 with pieces of 30 s.
 * "even_loudness": false leaves the pushed audio's loudness as it is, for a
 * recording whose loudness was evened out already: a job's copy for
 * listening back (sillok_job_save_audio) transcribes as the original did.
 * With "checkpoint", the job records in that file what it transcribed (and
 * aligned), one piece the model hears at a time. A job of the same recording,
 * options and transcription model with the same file goes on after the
 * pieces it finds there, first passing them to the segment callback, and
 * gives the transcript one run would have given; otherwise it starts the
 * file anew. Detecting speech and diarization run again in full, as they are
 * quick. The file is removed when the job succeeds and kept when it fails or
 * is cancelled: a new job with the same file then goes on, as after a crash.
 * A file that cannot be written is dropped and the job carries on. */
SILLOK_API sillok_status sillok_job_create(sillok_engine* engine,
                                           const char* options_json,
                                           sillok_job** out_job);

/* Appends decoded audio: `frames` frames of `channels` interleaved float
 * samples in [-1, 1] at `sample_rate` Hz (8000-192000). Rate and channel
 * count may change between calls; the recording stays one continuous 16 kHz
 * mono stream. NaN samples count as silence, infinities as full scale.
 * Transcript times count from the first sample pushed, so push the recording
 * from its start, with encoder priming already removed (decoders such as
 * AVAudioFile and ffmpeg do), for the times to match the original file.
 * The job keeps the recording in memory as 16-bit samples, about 115 MB an
 * hour; SILLOK_INTERNAL_ERROR ("out of memory") when it does not fit. */
SILLOK_API sillok_status sillok_job_push_audio(sillok_job* job,
                                               const float* samples,
                                               size_t frames,
                                               int32_t sample_rate,
                                               int32_t channels);

/* Fraction in [0, 1] of a file read or written, on the calling thread. */
typedef void (*sillok_file_progress_fn)(void* user_data, double fraction);

/* Decodes the audio of the file at `path` and pushes it as
 * sillok_job_push_audio() would, from the start of the file and without
 * encoder priming. Audio and video files alike: MP4/M4A/MOV, MP3, WAV, FLAC,
 * Ogg/Opus/Vorbis, WebM/MKV, AAC, AMR, WMA/ASF, AVI, MPEG-TS, AIFF, CAF; the
 * first audio stream is read. Blocks until the file is read; `progress` may
 * be NULL, and sillok_job_cancel() stops the read (SILLOK_CANCELLED). The
 * recording stays open: end it with sillok_job_end_audio() or run the job.
 * SILLOK_MEDIA_ERROR when the file cannot be opened or decoded. */
SILLOK_API sillok_status sillok_job_push_file(sillok_job* job, const char* path,
                                              sillok_file_progress_fn progress,
                                              void* user_data);

/* --- The job's recording ---------------------------------------------------
 * A job converts the pushed audio to mono at SILLOK_SAMPLE_RATE and evens out
 * its loudness: the recording it transcribes, which the transcript's times
 * refer to. Sample n lies at n * 1000 / SILLOK_SAMPLE_RATE milliseconds.
 * sillok_job_save_audio() keeps a copy of it for listening back, so that a
 * segment plays exactly what was transcribed without converting the audio a
 * second time. These functions must not be called while sillok_job_run() is
 * executing on the job. */
#define SILLOK_SAMPLE_RATE 16000

/* Ends the recording: no audio can be pushed afterwards. sillok_job_run()
 * does this itself; calling it again is allowed. */
SILLOK_API sillok_status sillok_job_end_audio(sillok_job* job);

/* Writes the recording to `path` as AAC in an M4A file (16 kHz mono,
 * 64 kbit/s, about 29 MB an hour) for the app to keep for listening back:
 * transcript times are positions in it, and it sounds as the transcription
 * heard it, with quiet passages raised. A job of the copy with
 * "even_loudness": false transcribes as one of the original does, so the
 * original is needed only once. Call after the recording ended.
 * Blocks until written; `progress` may be NULL, and sillok_job_cancel() stops
 * it (SILLOK_CANCELLED). An unfinished file is removed. SILLOK_MEDIA_ERROR
 * when the file cannot be written. */
SILLOK_API sillok_status sillok_job_save_audio(sillok_job* job, const char* path,
                                               sillok_file_progress_fn progress,
                                               void* user_data);

/* Segments of the transcript as the transcription stage finishes them, for
 * the app to show while the job runs: `segments_json` is an array of
 * segments as in the transcript document (below), without "speaker", each
 * with "chunk", the index of the piece of speech the model heard (see
 * "language" above) it comes from. A model with an aligner first gives what
 * it heard in a piece as a draft: one segment with "draft": true spanning the
 * piece, its text and no words. The align stage then passes all of the
 * piece's timed segments in one call, to replace the draft of the same
 * "chunk". Within a stage, pieces arrive in order. They are previews; the
 * transcript sillok_job_run() returns then adds speakers and splits long
 * segments.
 * Called on the thread running sillok_job_run(); the string lives for the
 * call. */
typedef void (*sillok_segments_fn)(void* user_data, const char* segments_json);

/* Sets the job's segment callback before it runs; NULL removes it. */
SILLOK_API sillok_status sillok_job_on_segments(sillok_job* job,
                                                sillok_segments_fn callback,
                                                void* user_data);

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
 *                           "speaker": 0}, ...]}, ...],
 *  "models": {"transcription": {"name": "...", "device": "...",
 *                               "fell_back_to_cpu": false}, ...}}
 * "models" names the model of each stage that ran: "voice_detection",
 * "transcription", "alignment" (a model with an aligner) and "diarization".
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
 * edited by the app) as "txt" or "json". `options_json` may be NULL
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
