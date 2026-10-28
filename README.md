# sillok

On-device transcription with speaker diarization, as a C++20 library with a C
ABI. Korean and English, including recordings that switch between them.

- Transcription: Whisper large-v3-turbo (q5_0) through whisper.cpp
- Voice activity detection: Silero through whisper.cpp
- Speaker diarization: Nemotron-3-Diarization through NeMo-Speech.cpp
- Compute: CPU, Vulkan, Metal; Core ML (Neural Engine) for the Whisper encoder on Apple

The library takes decoded PCM. Decoding files, choosing devices and
downloading models are left to the app; the `sillok` CLI is one such app.

## Layout

```
include/sillok/sillok.h   the C ABI, the only public header
src/core/                 model-independent logic, C++ standard library only
src/backends/             one directory per model; models.cpp picks the models
src/capi/                 the C ABI over the core (JSON in and out)
apps/cli/                 the sillok command-line app
tests/                    unit tests with fake models, integration probe
scripts/                  fetching dependencies and test audio, evaluation
```

## Build

Requirements: CMake 3.28+, a C++20 compiler, git, curl; Vulkan SDK headers
and `glslc` for `-DSILLOK_VULKAN=ON`.

```
scripts/fetch-deps.sh                # pinned sources into third_party/
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Options: `SILLOK_VULKAN` (GPU on Linux, Windows, Android), `SILLOK_COREML`
(Apple), `SILLOK_WITH_MODELS=OFF` (core, C ABI and tests only, no
third-party model code), `SILLOK_WERROR`.

Products: `libsillok` (shared, exports only `sillok_*`) and
`libsillok_static` (one archive including the third-party code; the app links
the system libraries: OpenMP, Vulkan or Apple frameworks).

## CLI

```
sillok models download               # into the default model directory
sillok transcribe lecture.m4a --diarize --format srt --output lecture.srt
sillok transcribe talk.wav --language ko --term "Kubernetes" --device gpu
sillok export lecture.json --format txt --speaker-label "화자 {n}"
```

16 kHz mono WAV is read directly; other formats are decoded by `ffmpeg`.
`SILLOK_LOG=1` shows the model libraries' logs.

## Library use

```c
sillok_engine* engine;
sillok_engine_create("{\"models_dir\": \"/path/to/models\", \"device\": \"gpu\"}", &engine);
sillok_job* job;
sillok_job_create(engine, "{\"language\": \"auto\", \"diarize\": true}", &job);
sillok_job_push_audio(job, samples, frames, 48000, 2);   /* repeat as decoded */
char* transcript;
sillok_job_run(job, on_progress, user_data, &transcript); /* blocks; cancel from any thread */
char* srt;
sillok_export(transcript, "srt", "{\"speaker_label\": \"Speaker {n}\"}", &srt);
```

The transcript is JSON with millisecond times in the original recording,
words with confidence, and speaker ids that the app can name. See
`include/sillok/sillok.h` for the schemas and the threading rules.

## Evaluation

```
scripts/fetch-test-audio.sh          # FLEURS ko/en, AMI ES2004a (CC BY 4.0)
python3 -I scripts/eval/make_sets.py tests/data/audio
python3 -I scripts/eval/run_eval.py --build build --models models \
    --audio tests/data/audio --device gpu --out eval/gpu
```

## Licenses of the models

Whisper (MIT), Silero VAD (MIT), Nemotron-3-Diarization (OpenMDW-1.1).
