#!/usr/bin/env pwsh
# Build the modem_controller project using Visual Studio 2022

param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [switch]$Clean,
    [switch]$Test,
    [switch]$NoTests
)

$ErrorActionPreference = "Stop"
$BuildDir = Join-Path $PSScriptRoot "build"

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning build directory..."
    Remove-Item -Recurse -Force $BuildDir
}

Write-Host "Configuring with Visual Studio 17 2022..."
$cmakeArgs = @("-B", $BuildDir, "-G", "Visual Studio 17 2022")
if ($NoTests) {
    $cmakeArgs += "-DMODEM_BUILD_TESTS=OFF"
}
$cmakeArgs += $PSScriptRoot
cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Building ($Config)..."
cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Build succeeded."

$OutDir = Join-Path $PSScriptRoot "out"
if (Test-Path $OutDir) {
    Remove-Item -Recurse -Force $OutDir
}
Copy-Item (Join-Path $BuildDir "Release") -Destination $OutDir -Recurse
Write-Host "Copied Release/ to out/"

if ($Test) {
    Write-Host "Running tests..."
    ctest --test-dir $BuildDir --output-on-failure -C $Config
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
