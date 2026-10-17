# Sillok .NET

- `Sillok.Core`: C++ 코어(`Native/`) 연결, 진행 관리, 작업 큐, 모델 관리. Windows 앱이 쓴다.
- `Sillok.Core.Tests`: xUnit v3 테스트. C++ 코어 공유 라이브러리를 불러 실제 모델로도 검사한다.
- `Sillok.Windows`: WinUI 3 앱 (x64, ARM64). Windows에서만 빌드된다.

명령줄 도구 `sillok`은 C++ 코어와 함께 `Native/cli`에 있다.

## Windows 빌드

필요한 것: Visual Studio 2026 (.NET 데스크톱 개발, C++를 사용한 데스크톱 개발), .NET 10 SDK, Vulkan SDK.

```powershell
git submodule update --init Vendor/whisper.cpp Vendor/NeMo-Speech.cpp
scripts/windows/build-native.ps1 -Arch x64
dotnet build dotnet/Sillok.Windows -p:Platform=x64
```

`build-native.ps1`은 앱이 쓰는 `sillok.dll`과 CLI `sillok.exe`를 함께 만든다. ARM64는 `-Arch ARM64`와 `-p:Platform=ARM64`로 빌드하고, ARM64 기기에서 직접 빌드하는 것이 가장 간단하다.

## 테스트 (macOS·Windows)

```sh
cmake -S Native -B build/native -DCMAKE_BUILD_TYPE=Release -DSILLOK_SHARED=ON
cmake --build build/native -j
scripts/fetch-test-models.sh
dotnet test --project dotnet/Sillok.Core.Tests
```

Windows에서는 `cmake --build build/native --config Release`가 만든 `build/native/Release/sillok.dll`을 쓴다.
