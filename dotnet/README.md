# Sillok .NET

- `Sillok.Core`: C++ 코어(`Native/`) 연결, 파이프라인, 체크포인트, 작업 큐, 모델 관리. Windows·Linux·macOS 공용.
- `Sillok.Core.Tests`: xUnit v3 테스트. C++ 코어 공유 라이브러리를 불러 실제 모델로도 검사한다.
- `Sillok.Cli`: `sillok` 명령줄 도구. WAV는 직접 읽고 그 밖의 형식은 ffmpeg로 변환한다.
- `Sillok.Windows`: WinUI 3 앱 (x64, ARM64). Windows에서만 빌드된다.

## Windows 앱 빌드

필요한 것: Visual Studio 2026 (.NET 데스크톱, C++ 데스크톱 워크로드), .NET 10 SDK, CMake, Vulkan SDK.

```powershell
git submodule update --init Vendor/whisper.cpp Vendor/NeMo-Speech.cpp
scripts/windows/build-native.ps1 -Arch x64
dotnet build dotnet/Sillok.Windows -p:Platform=x64
```

ARM64는 `-Arch ARM64`와 `-p:Platform=ARM64`로 빌드한다. Vulkan 셰이더 생성 도구 때문에 ARM64 기기에서 직접 빌드하는 것이 가장 간단하다.

## 테스트와 CLI (macOS·Linux·Windows)

```sh
cmake -S Native -B build/native -DCMAKE_BUILD_TYPE=Release -DSILLOK_SHARED=ON
cmake --build build/native -j
scripts/fetch-test-models.sh
dotnet test --project dotnet/Sillok.Core.Tests
dotnet run --project dotnet/Sillok.Cli -- transcribe audio.wav --models models --diarize
```

Windows에서는 `build/native` 대신 `cmake --build build/native --config Release`가 만든 `build/native/Release/sillok.dll`을 쓴다.
