# Sillok for macOS

A native SwiftUI app for macOS 15 or later on Apple silicon, over the C ABI
(`include/sillok/sillok.h`). It downloads the models on first launch,
transcribes audio and video files one after another, and keeps each
recording with its transcript for listening back, naming the speakers and
exporting as text or JSON.

```
Sillok.xcodeproj               the app target
Sillok/                        views and app state, by feature
Packages/SillokKit/            Swift over the C ABI, testable without Xcode
  Sources/SillokEngine/        catalog, engine, jobs, transcripts, export
  Sources/SillokStore/         model downloads, the recordings on disk
scripts/build-core.sh          builds the library and packages it as XCFrameworks
```

## Build

Requirements: Xcode 16 or later, and the library's own requirements (see the
top-level README).

```
git submodule update --init --depth 1
apps/mac/scripts/build-core.sh
open apps/mac/Sillok.xcodeproj
```

`build-core.sh` builds the library with Core ML into `build/mac` and writes
`CSillok.xcframework` and FFmpeg's libraries as XCFrameworks into
`Packages/SillokKit/Frameworks`, which Xcode embeds and signs. Run it again
after changing the library; the first run takes a few minutes, later ones only
rebuild what changed.

## Tests

```
cd apps/mac/Packages/SillokKit && swift test
```

With downloaded models and a recording, the tests also transcribe it:

```
SILLOK_TEST_MODELS=<models directory> SILLOK_TEST_AUDIO=<audio file> swift test
```

## Data

The app runs in the App Sandbox, under the bundle id
`io.github.linkedlatch.sillok`. Its files live in
`~/Library/Containers/io.github.linkedlatch.sillok/Data/Library/Application Support`:

```
Models/                  the catalog's files; .partial files resume downloads
Recordings/<id>/         audio.m4a (16 kHz mono), transcript.json, meta.json
```

Settings (the transcription model, CPU or GPU, the Neural Engine and the
number of threads) apply from the next transcription.
