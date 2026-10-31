# sillok

On-device transcription with speaker diarization, as a C++20 library with a C
ABI. Korean and English, including recordings that switch between them.

- Transcription: Whisper large-v3-turbo (q5_0) through whisper.cpp
- Voice activity detection: Silero through whisper.cpp
- Speaker diarization: Nemotron-3-Diarization through NeMo-Speech.cpp
- Compute: CPU, Vulkan, Metal; Core ML (Neural Engine) for the Whisper encoder on Apple

The library reads audio and video files itself (FFmpeg, audio decoders only)
or takes decoded PCM. Choosing devices and downloading models are left to the
app; the `sillok` CLI is one such app.

## Layout

```
include/sillok/sillok.h   the C ABI, the only public header
src/core/                 model-independent logic, C++ standard library only
src/backends/             one directory per model; models.cpp picks the models
src/media/                 reading audio and video files with FFmpeg
src/capi/                 the C ABI over the core (JSON in and out)
apps/cli/                 the sillok command-line app
tests/                    unit tests with fake models, integration probe; tests/data/media: short test files
third_party/              whisper.cpp, NeMo-Speech.cpp, FFmpeg, googletest (submodules); nlohmann/json header
scripts/                  fetching test audio, evaluation
patches/                  local patches to the submodules (CMake applies them)
```

`patches/whisper.cpp` lets a cancel stop a Whisper pass midway on the CPU
(upstream checks only between passes), and reuses the encoder output when
language detection and the transcription encode the same audio (auto
language ran the encoder twice per window). After updating whisper.cpp, run
the integration tests: one of them fails if the cancel patch no longer takes
effect; compare an auto-language run's output and time with the previous
version for the other.

## Build

Requirements: CMake 3.28+, a C++20 compiler, git, and `make` with a POSIX
shell for FFmpeg's own build (on Windows: MSYS2 at `C:\msys64` with
`pacman -S make diffutils`, building from an MSVC developer prompt); Vulkan
SDK headers and `glslc` for `-DSILLOK_VULKAN=ON`.

Clone without `--recursive` (or `--recurse-submodules`): NeMo-Speech.cpp's
own submodules are unused, and shallow submodules are enough.

```
git clone https://github.com/linkedlatch/sillok.git && cd sillok
git submodule update --init --depth 1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Options: `SILLOK_VULKAN` (GPU on Linux, Windows, Android), `SILLOK_COREML`
(Apple), `SILLOK_WITH_MODELS=OFF` (core, C ABI and tests only, no
third-party model code), `SILLOK_WERROR`.

Products: `libsillok` (shared, exports only `sillok_*`) and
`libsillok_static` (one archive including the third-party code; the app links
the system libraries: OpenMP, Vulkan or Apple frameworks). Both need FFmpeg's
shared libraries (`libavformat`, `libavcodec`, `libswresample`, `libavutil`,
in `build/third_party/ffmpeg/install`) beside them; FFmpeg is built under the
LGPL, without GPL or nonfree parts, and stays replaceable that way.

## CLI

```
sillok models list                   # * marks the recommended models
sillok models download               # into the default model directory
sillok models download --model whisper-small-q5_1   # another transcription model
sillok transcribe lecture.m4a --diarize --format srt --output lecture.srt
sillok transcribe talk.wav --language ko --term "Kubernetes" --device gpu
sillok transcribe talk.wav --model whisper-small-q5_1
sillok export lecture.json --format txt --speaker-label "화자 {n}"
```

Audio and video files (M4A/MP4, MP3, WAV, FLAC, Ogg, Opus, WebM, AMR, WMA,
...) are read by the library. `SILLOK_LOG=1` shows the model libraries' and
FFmpeg's logs.

## Library use

```c
sillok_engine* engine;
sillok_engine_create("{\"models_dir\": \"/path/to/models\", \"device\": \"gpu\"}", &engine);
sillok_job* job;
sillok_job_create(engine, "{\"language\": \"auto\", \"diarize\": true}", &job);
sillok_job_push_file(job, "/path/to/lecture.m4a", on_read, user_data);
/* or sillok_job_push_audio(job, samples, frames, 48000, 2) as you decode */
char* transcript;
sillok_job_run(job, on_progress, user_data, &transcript); /* blocks; cancel from any thread */
char* srt;
sillok_export(transcript, "srt", "{\"speaker_label\": \"Speaker {n}\"}", &srt);
```

The transcript is JSON with millisecond times in the original recording,
words with confidence, and speaker ids that the app can name. Apps that
keep a copy for listening back read the job's own 16 kHz mono recording
(`sillok_job_read_audio`), the audio those times refer to. See
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
