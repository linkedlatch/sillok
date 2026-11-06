# sillok

On-device transcription with speaker diarization, as a C++20 library with a C
ABI. Korean and English, including recordings that switch between them.

- Transcription: Whisper large-v3-turbo (q5_0) through whisper.cpp
- Voice activity detection: Silero through whisper.cpp
- Speaker diarization: Nemotron-3-Diarization through NeMo-Speech.cpp
- Compute: CPU, Vulkan, Metal; the NPU for the Whisper encoder on Apple (Core ML, the Neural Engine)

The library reads audio and video files itself (FFmpeg, audio decoders only)
or takes decoded PCM. Choosing devices and downloading models are left to the
app; the `sillok` CLI is one such app.

## Layout

```
include/sillok/sillok.h   the C ABI, the only public header
src/core/                 model-independent logic, C++ standard library only
src/backends/             one directory per model; models.cpp picks the models
src/media/                FFmpeg: reading audio and video files, converting audio to 16 kHz mono
src/capi/                 the C ABI over the core (JSON in and out)
apps/cli/                 the sillok command-line app
apps/app/                 the Flutter app (Windows, macOS on Apple silicon)
tests/                    unit tests with fake models, integration probe; tests/data/media: short test files
third_party/              whisper.cpp, NeMo-Speech.cpp, FFmpeg, googletest, nlohmann/json (submodules)
scripts/                  fetching test audio, evaluation
```

To update a submodule to a release, fetch and check out its tag, then commit
the submodule's new commit. NeMo's llama.cpp follows the commit NeMo records,
so fetch it again after updating NeMo:

```
git -C third_party/NeMo-Speech.cpp fetch --depth 1 origin tag v0.4.0
git -C third_party/NeMo-Speech.cpp checkout v0.4.0
git -C third_party/NeMo-Speech.cpp submodule update --init --depth 1 llama.cpp
git add third_party/NeMo-Speech.cpp
```

After updating NeMo-Speech.cpp, compare the diarization (`ami` and
`korean_speakers` in `scripts/eval/run_eval.py`) with the previous version, and
check that the sources listed in `cmake/third_party.cmake` still build.

## Build

Requirements: CMake 3.28+, a C++20 compiler, git, and `make` with a POSIX
shell for FFmpeg's own build (on Windows: MSYS2 at `C:\msys64` with
`pacman -S make diffutils`, building from an MSVC developer prompt); Vulkan
SDK headers and `glslc` for `-DSILLOK_VULKAN=ON`.

Clone without `--recursive` (or `--recurse-submodules`): of NeMo-Speech.cpp's
own submodules, only llama.cpp is used (its file reading, for NeMo's model
loader; its ggml is not built, whisper.cpp's is the only one), so it is
fetched by itself. Shallow submodules are enough.

```
git clone https://github.com/linkedlatch/sillok.git && cd sillok
git submodule update --init --depth 1
git -C third_party/NeMo-Speech.cpp submodule update --init --depth 1 llama.cpp
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Options: `SILLOK_VULKAN` (GPU on Linux, Windows), `SILLOK_COREML`
(Apple), `SILLOK_WITH_MODELS=OFF` (core, C ABI and tests only, no
third-party model code), `SILLOK_WERROR`.

Product: `libsillok` (shared, exports only `sillok_*`). It needs FFmpeg's
shared libraries (`libavformat`, `libavcodec`, `libavfilter`, `libswresample`, `libavutil`,
in `build/third_party/ffmpeg/install`) beside it; FFmpeg is built under the
LGPL, without GPL or nonfree parts, and stays replaceable that way.

## CLI

```
sillok models list                   # * marks the recommended models
sillok models download               # into the default model directory
sillok models download --model whisper-large-v3-q5_0   # another transcription model
sillok transcribe lecture.m4a --diarize --format srt --output lecture.srt
sillok transcribe talk.wav --language ko --term "Kubernetes" --device gpu
sillok transcribe talk.wav --model whisper-large-v3-q5_0
sillok export lecture.json --format txt --speaker-label "화자 {n}"
```

Audio and video files (M4A/MP4, MP3, WAV, FLAC, Ogg, Opus, WebM, AMR, WMA,
...) are read by the library. `SILLOK_LOG=1` shows the model libraries' and
FFmpeg's logs.

## App

`apps/app` is one Flutter app for Windows (x86_64) and macOS (Apple silicon),
over the C ABI through `dart:ffi`. Its build hook (`apps/app/hook/build.dart`) builds the
library with this repository's CMake project for the target and bundles it
with FFmpeg's libraries, so a plain `flutter run` or `flutter build` is
enough once the library's own requirements are met (see Build; on Windows,
the Vulkan SDK in `C:\VulkanSDK` adds the GPU). The first build of each
target compiles the library and takes a few minutes.

```
cd apps/app
flutter test                                    # the hook builds the library for this machine
SILLOK_TEST_MODELS=../../models \
    SILLOK_TEST_AUDIO=../../tests/data/audio/fleurs/en_us/1003119935936341070.wav flutter test
flutter build windows                           # or macos
flutter test integration_test -d windows        # the built app; or -d macos
```

Each push builds the apps in CI (the `app` workflow): `sillok-windows-x64` and
`sillok-macos` under the run's artifacts. The macOS
app has an ad-hoc signature but is not notarized, so macOS refuses to open it
the first time it comes from a download. Try to open it once, then choose
Open Anyway in System Settings > Privacy & Security and enter your password;
it opens normally from then on. Alternatively, remove the download mark
before opening it:

```
xattr -dr com.apple.quarantine Sillok.app
```

A copy built on the Mac itself has no download mark and opens directly.

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
keep a copy for listening back save the job's own 16 kHz mono recording
(`sillok_job_save_audio`), the audio those times refer to, with its loudness
evened out as the transcription heard it. See
`include/sillok/sillok.h` for the schemas and the threading rules.

## Evaluation

```
scripts/fetch-test-audio.sh          # FLEURS ko/en, AMI ES2004a (CC BY 4.0)
python3 -I scripts/eval/make_sets.py tests/data/audio
python3 -I scripts/eval/run_eval.py --build build --models models \
    --audio tests/data/audio --device gpu --out eval/gpu
```

The `lecture` step, not run by default, scores sentences spliced into a real
lecture (`sillok transcribe` its recording to JSON first; needs numpy):

```
python3 scripts/eval/make_lecture_set.py lecture.wav lecture.json tests/data/audio
python3 -I scripts/eval/run_eval.py ... --only lecture
```

## Licenses of the models

Whisper (MIT), Silero VAD (MIT), Nemotron-3-Diarization (OpenMDW-1.1).
