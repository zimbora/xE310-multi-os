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

if ($Test) {
    Write-Host "Running tests..."
    wsl --exec bash -c "cd '$WslPath' && ctest --test-dir build-linux --output-on-failure"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
