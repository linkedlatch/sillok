# Sillok .NET

- `Sillok.Core`: bindings to the C++ core (`Native/`), job orchestration, job queue, and model management. Used by the Windows app.
- `Sillok.Core.Tests`: xUnit v3 tests. They load the C++ core as a shared library and also run against the real models.
- `Sillok.Windows`: WinUI 3 app (x64, ARM64). Builds on Windows only.

The `sillok` command line tool lives with the C++ core in `Native/cli`.

## Building on Windows

Requirements: Visual Studio 2026 (or Build Tools) with the ".NET desktop development", "Desktop development with C++" and "WinUI application development" workloads, the .NET 10 SDK, and the Vulkan SDK.

```powershell
git submodule update --init Vendor/whisper.cpp Vendor/NeMo-Speech.cpp
powershell -ExecutionPolicy Bypass -File scripts\windows\build.ps1 -Arch x64
```

The script builds the native core, the `sillok` command line tool, and the app:

- `build/windows-x64/app/Sillok.exe`: the app. The folder is self-contained (it includes the .NET and Windows App SDK runtimes), so it can be zipped and run on another PC without installing anything.
- `build/windows-x64/cli/Release/sillok.exe`: the command line tool.

The app and the command line tool share models and data under `%LOCALAPPDATA%\Sillok`. For ARM64 use `-Arch ARM64`; building on an ARM64 machine is the simplest option.

For debugging, run `scripts\windows\build-native.ps1 -Arch x64` once, then open `dotnet/Sillok.Windows.slnx` in Visual Studio, pick the x64 or ARM64 platform, and press F5.

## Tests (macOS and Windows)

```sh
cmake -S Native -B build/native -DCMAKE_BUILD_TYPE=Release -DSILLOK_SHARED=ON
cmake --build build/native -j
scripts/fetch-test-models.sh
dotnet test --project dotnet/Sillok.Core.Tests
```

On Windows the tests use `build/native/Release/sillok_core.dll` produced by `cmake --build build/native --config Release`.
