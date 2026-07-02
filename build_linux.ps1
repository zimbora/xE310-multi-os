#!/usr/bin/env pwsh
# Build the modem_controller project for Linux using WSL

param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [switch]$Clean,
    [switch]$Test
)

$ErrorActionPreference = "Stop"
$WslPath = wsl wslpath -a "$PSScriptRoot"

if ($Clean) {
    Write-Host "Cleaning build directory..."
    wsl --exec bash -c "rm -rf '$WslPath/build-linux'"
}

Write-Host "Configuring for Linux ($Config)..."
wsl --exec bash -c "cd '$WslPath' && cmake -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Building..."
wsl --exec bash -c "cd '$WslPath' && cmake --build build-linux"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Build succeeded."

$OutDir = Join-Path $PSScriptRoot "out"
if (Test-Path $OutDir) {
    Remove-Item -Recurse -Force $OutDir
}
New-Item -ItemType Directory -Path $OutDir | Out-Null

# Copy library
$LibPath = wsl --exec bash -c "find '$WslPath/build-linux' -maxdepth 1 -name 'libmodem_xe310.*' -print -quit"
if ($LibPath) {
    $WinLibPath = wsl wslpath -w $LibPath.Trim()
    Copy-Item $WinLibPath -Destination $OutDir
}

# Copy executables
foreach ($exe in @("modem_app", "test_xe310")) {
    $ExePath = wsl --exec bash -c "find '$WslPath/build-linux' -maxdepth 1 -name '$exe' -print -quit"
    if ($ExePath) {
        $WinExePath = wsl wslpath -w $ExePath.Trim()
        Copy-Item $WinExePath -Destination $OutDir
    }
}
Write-Host "Copied build outputs to out/"

if ($Test) {
    Write-Host "Running tests..."
    wsl --exec bash -c "cd '$WslPath' && ctest --test-dir build-linux --output-on-failure"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
