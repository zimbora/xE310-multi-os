# Build Guide

## Build System

- CMake is the primary build system.
- Zephyr builds integrate via Zephyr's CMake system.
- Desktop builds use standard CMake workflows.

## Desktop (Windows)

```bash
.\build_vs.ps1
.\build_vs.ps1 -Clean # clean build
.\build_vs.ps1 -Clean -Test # build and run tests
.\build_vs.ps1 -Clean -Config Debug -Test # clean, debug build, tests
.\build_vs.ps1 -Clean -NoTests
.\build_vs.ps1 -DTEST=OFF
```

## Linux

```bash
.\build_linux.ps1
```

## Embedded (Zephyr)

```bash
.\build_zephyr.ps1
```

## Log Levels

Log verbosity is selected at compile time with CMake cache variables:

- `MODEM_LOG_LEVEL` for `MODEM_LOG_*`
- `NETWORK_LOG_LEVEL` for `NETWORK_LOG_*`

Supported values:

- `0` = none
- `1` = error
- `2` = warning
- `3` = info
- `4` = debug

Example:

```bash
cmake -S . -B build -DMODEM_LOG_LEVEL=4 -DNETWORK_LOG_LEVEL=2
cmake --build build --config Release
```
