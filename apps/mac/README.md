# Sillok for macOS

A native SwiftUI app for macOS 15 or later on Apple silicon, over the C ABI
(`include/sillok/sillok.h`).

```
Sillok.xcodeproj           the app target: views only
Sillok/                    app sources and resources
Packages/SillokKit/        Swift wrapper over the C ABI, testable without Xcode
scripts/build-core.sh      builds the library and packages it as XCFrameworks
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

```
cd apps/mac/Packages/SillokKit && swift test
```

The app runs in the App Sandbox; its models and recordings live in its
container.
