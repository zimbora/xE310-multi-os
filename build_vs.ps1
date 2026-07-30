#!/usr/bin/env pwsh
# Build the modem_controller project using Visual Studio 2022

param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [switch]$Clean,
    [switch]$Test,
    [switch]$WithTests,
    [switch]$NoTests,
    [Alias("D")]
    [string[]]$Define,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CMakeArgs
)

$ErrorActionPreference = "Stop"
$BuildDir = Join-Path $PSScriptRoot "build"

if ($Clean) {
    if (Test-Path $BuildDir) {
        Write-Host "Cleaning build directory..."
        Remove-Item -Recurse -Force $BuildDir
    }
    # Remove any in-source cmake artifacts that would cause cmake to ignore -B <BuildDir>
    foreach ($artifact in @("CMakeCache.txt", "CMakeFiles", "cmake_install.cmake", "CTestTestfile.cmake")) {
        $artifactPath = Join-Path $PSScriptRoot $artifact
        if (Test-Path $artifactPath) {
            Write-Host "Removing in-source artifact: $artifact"
            Remove-Item -Recurse -Force $artifactPath
        }
    }
}

Write-Host "Configuring with Visual Studio 17 2022..."
$cmakeArgs = @("-B", $BuildDir, "-G", "Visual Studio 17 2022")

# Build tests only when explicitly requested.
# -Test implies tests must be built so they can run.
$enableTests = $WithTests -or $Test
if ($NoTests) {
    $enableTests = $false
}

if ($enableTests) {
    $cmakeArgs += "-DMODEM_BUILD_TESTS=ON"
} else {
    $cmakeArgs += "-DMODEM_BUILD_TESTS=OFF"
}

$forwardedArgs = @()
foreach ($d in $Define) {
    $forwardedArgs += "-D$d"
}

# PowerShell can consume tokens like -DNAME=VALUE before they reach script parameters.
# Recover them from the original invocation line so users can call:
#   .\build_vs.ps1 -DMODEM_NETWORK_LOG=4 -DMODEM_MODEM_LOG=4
if ($MyInvocation.Line) {
    $rawDefineMatches = [regex]::Matches($MyInvocation.Line, '(?<!\S)-D([A-Za-z_][A-Za-z0-9_]*=[^\s]+)')
    foreach ($m in $rawDefineMatches) {
        $forwardedArgs += "-D$($m.Groups[1].Value)"
    }
}

$forwardedArgs += $CMakeArgs
$forwardedArgs = $forwardedArgs | Select-Object -Unique

foreach ($arg in $forwardedArgs) {
    if ($arg -match "^-DMODEM_NETWORK_LOG=(.+)$") {
        $cmakeArgs += "-DNETWORK_LOG_LEVEL=$($matches[1])"
        continue
    }
    if ($arg -match "^-DMODEM_MODEM_LOG=(.+)$") {
        $cmakeArgs += "-DMODEM_LOG_LEVEL=$($matches[1])"
        continue
    }
    $cmakeArgs += $arg
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
Copy-Item (Join-Path $BuildDir $Config) -Destination $OutDir -Recurse
Write-Host "Copied $Config/ to out/"

if ($Test) {
    Write-Host "Running tests..."
    ctest --test-dir $BuildDir --output-on-failure -C $Config
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
