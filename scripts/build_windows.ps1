# Build AeroGlow on Windows with Visual Studio 2022 or newer (x64).
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 [-Config Release|Debug] [-AaxSdk C:\SDKs\aax-sdk]
param(
    [ValidateSet("Release", "Debug")] [string] $Config = "Release",
    [string] $AaxSdk = "",
    [switch] $Avx2
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

# No -G: CMake uses the newest installed Visual Studio (2022 or 2026).
$cmakeArgs = @("-S", ".", "-B", "build-win", "-A", "x64")
if ($AaxSdk)       { $cmakeArgs += "-DAEROGLOW_AAX_SDK_PATH=$AaxSdk" }
if ($Avx2)         { $cmakeArgs += "-DAEROGLOW_ENABLE_AVX2=ON" }
if ($env:JUCE_DIR) { $cmakeArgs += "-DJUCE_DIR=$($env:JUCE_DIR)" }

cmake @cmakeArgs
cmake --build build-win --config $Config --parallel
ctest --test-dir build-win -C $Config --output-on-failure

Write-Host "`nArtefacts:"
Get-ChildItem "build-win\AeroGlow_artefacts\$Config" -Directory | ForEach-Object { Get-ChildItem $_.FullName }
