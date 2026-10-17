param(
    [ValidateSet("x64", "ARM64")]
    [string]$Arch = "x64"
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot/../.."
$app = Join-Path $root "build/windows-$($Arch.ToLowerInvariant())/app"

& (Join-Path $PSScriptRoot "build-native.ps1") -Arch $Arch
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

dotnet publish (Join-Path $root "dotnet/Sillok.Windows/Sillok.Windows.csproj") -c Release -p:Platform=$Arch -o $app
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "==> $(Join-Path $app 'Sillok.exe')"
