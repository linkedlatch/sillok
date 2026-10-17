param(
    [ValidateSet("x64", "ARM64")]
    [string]$Arch = "x64"
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot/../.."
$build = Join-Path $root "build/windows-$($Arch.ToLowerInvariant())"

if (-not $env:VULKAN_SDK) {
    throw "The Vulkan SDK is required. Install it from https://vulkan.lunarg.com and open a new terminal."
}

cmake -S (Join-Path $root "Native") -B $build -A $Arch -DSILLOK_SHARED=ON -DSILLOK_BUILD_TESTS=OFF
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $build --config Release --target sillok sillok_cli --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "==> $(Join-Path $build 'Release/sillok.dll')"
Write-Host "==> $(Join-Path $build 'cli/Release/sillok.exe')"
