# Sillok .NET

- `Sillok.Core`: bindings to the C++ core (`Native/`), job orchestration, job queue, and model management. Used by the Windows app.
- `Sillok.Core.Tests`: xUnit v3 tests. They load the C++ core as a shared library and also run against the real models.
- `Sillok.Windows`: WinUI 3 app (x64, ARM64). Builds on Windows only.

The `sillok` command line tool lives with the C++ core in `Native/cli`.

## Building on Windows

Requirements: Visual Studio 2026 with the ".NET desktop development" and "Desktop development with C++" workloads, the .NET 10 SDK, and the Vulkan SDK.

```powershell
git submodule update --init Vendor/whisper.cpp Vendor/NeMo-Speech.cpp
scripts/windows/build-native.ps1 -Arch x64
dotnet build dotnet/Sillok.Windows -p:Platform=x64
```

`build-native.ps1` builds both `sillok.dll` for the app and the `sillok.exe` command line tool. For ARM64 use `-Arch ARM64` and `-p:Platform=ARM64`; building on an ARM64 machine is the simplest option.

## Tests (macOS and Windows)

```sh
cmake -S Native -B build/native -DCMAKE_BUILD_TYPE=Release -DSILLOK_SHARED=ON
cmake --build build/native -j
scripts/fetch-test-models.sh
dotnet test --project dotnet/Sillok.Core.Tests
```

On Windows the tests use `build/native/Release/sillok.dll` produced by `cmake --build build/native --config Release`.
