# Modem Controller - Project Guidelines

## Overview

C++ library to control a modem via serial UART interface using AT commands.

## Target Platforms

- **Desktop**: Windows, macOS, Linux
- **Embedded**: Zephyr OS with nRF54 microcontroller

All code must compile and run correctly on every target platform.

## Architecture

- Use a **Hardware Abstraction Layer (HAL)** to isolate platform-specific serial/UART code
  - Desktop platforms use OS-native serial APIs (Win32, POSIX)
  - Zephyr uses the Zephyr UART driver API
- Core modem logic (AT command parsing, state machine) must be **platform-independent**
- Prefer composition over inheritance for platform abstractions

## Code Style

- C++17 standard (compatible with Zephyr's toolchain)
- Use `#pragma once` for include guards
- Keep headers minimal — forward-declare where possible
- No exceptions on embedded targets — use error codes or `std::expected`-style patterns
- No dynamic memory allocation in hot paths on embedded targets

### Naming Conventions

| Entity             | Convention                                                              |
|--------------------|-------------------------------------------------------------------------|
| Functions          | `snake_case` (e.g. `read_sensor_value`, `process_frame`)               |
| Variables          | `camelCase` (e.g. `sampleCount`, `rxBuffer`)                           |
| Bool variables     | `fPascalCase` (e.g. `fIsConnected`, `fHasData`, `fEnabled`)            |
| Global variables   | `g_` prefix + `camelCase` (e.g. `g_sensorState`, `g_txBuffer`)         |
| Static variables   | `s_` prefix + `camelCase` (e.g. `s_instanceCount`, `s_cachedValue`)    |
| Types (class/struct) | `PascalCase` (e.g. `SensorDriver`, `FrameHeader`)                    |
| Enums              | `PascalCase` for type, `PascalCase` for values (e.g. `Error::HardwareFault`) |
| Constants / macros | `UPPER_SNAKE_CASE` (e.g. `MAX_FRAME_SIZE`, `DEFAULT_TIMEOUT_MS`)       |
| Namespaces         | `snake_case` (e.g. `anova::sensor_driver`)                             |
| Template params    | `PascalCase` (e.g. `template <typename ValueType>`)                    |

### Style Checker

Run the naming convention checker:

```bash
python3 scripts/check_code_style.py
```

With verbose output and summary:

```bash
python3 scripts/check_code_style.py --verbose --summary
```

## Build System

- **CMake** as the primary build system
- Zephyr builds integrate via Zephyr's CMake system
- Desktop builds use standard CMake workflows

### Desktop (Windows)
```bash
.\build_vs.ps1
.\build_vs.ps1 -Clean # clean build
.\build_vs.ps1 -Clean -Test # build and run tests
.\build_vs.ps1 -Clean -Config Debug -Test # clean, debug build, tests
.\build_vs.ps1  -Clean -NoTests
.\build_vs.ps1 -DTEST=OFF
```

### Linux
```bash
.\build_linux.ps1
```

### Embedded (Zephyr)
```bash
.\build_zephyr.ps1
```

### Log Levels

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

## Conventions

- AT commands are sent as null-terminated strings over UART
- All UART operations must support both blocking and non-blocking modes
- Timeouts must be configurable for every AT command exchange
- Logging must be abstracted (use Zephyr logging on embedded, stdout/spdlog on desktop)

## Code Formatting

Format all source files with [clang-format](https://clang.llvm.org/docs/ClangFormat.html):

```bash
clang-format -i src/*.cpp src/hal/*.cpp include/modem/*.h
```

Check formatting without modifying files:

```bash
clang-format --dry-run --Werror src/*.cpp src/hal/*.cpp include/modem/*.h
```

## Static Analysis

Install [Cppcheck](https://cppcheck.sourceforge.io/) and run:

```bash
cppcheck --enable=all --inline-suppr --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=normalCheckLevelMaxBranches --suppress=checkersReport --error-exitcode=1 --std=c++17 -I include src/ include/
```

To generate a detailed checkers report:

### Desktop (Windows)
```bash
cppcheck --project=cppcheck-windows.cppcheck --inline-suppr --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=normalCheckLevelMaxBranches --error-exitcode=1 --std=c++17
```

### Embedded (Zephyr)
```bash
cppcheck --project=cppcheck-zephyr.cppcheck --std=c++17 --inline-suppr --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=normalCheckLevelMaxBranches --error-exitcode=1 --std=c++17
```

## Testing

- Unit tests use Google Test (desktop only)
- Mock the HAL interface for testability
- Integration tests run against real hardware or a modem simulator

```bash
cd build
ctest --output-on-failure
```

```bash
.\build\tests\Release\test_network_lte.exe
```


## Directory Structure

```
include/          — Public headers
src/              — Implementation files
src/hal/          — Hardware abstraction layer implementations
tests/            — Unit and integration tests
cmake/            — CMake modules and toolchain files
```
