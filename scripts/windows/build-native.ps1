param(
    [ValidateSet("x64", "ARM64")]
    [string]$Arch = "x64"
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot/../.."
$build = Join-Path $root "build/windows-$($Arch.ToLowerInvariant())"

if (-not $env:VULKAN_SDK) {
    throw "Vulkan SDK가 필요합니다. https://vulkan.lunarg.com 에서 설치한 뒤 새 터미널에서 다시 실행하세요"
}

cmake -S (Join-Path $root "Native") -B $build -A $Arch -DSILLOK_SHARED=ON -DSILLOK_BUILD_TESTS=OFF
cmake --build $build --config Release --target sillok sillok_cli --parallel
Write-Host "==> $(Join-Path $build 'Release/sillok.dll')"
Write-Host "==> $(Join-Path $build 'cli/Release/sillok.exe')"
