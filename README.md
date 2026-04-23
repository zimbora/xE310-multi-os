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
- Use `snake_case` for functions and variables, `PascalCase` for classes and structs
- Use `#pragma once` for include guards
- Keep headers minimal — forward-declare where possible
- No exceptions on embedded targets — use error codes or `std::expected`-style patterns
- No dynamic memory allocation in hot paths on embedded targets

## Build System

- **CMake** as the primary build system
- Zephyr builds integrate via Zephyr's CMake system
- Desktop builds use standard CMake workflows

### Desktop (Windows)
```bash
.\build_vs.ps1
```

### Linux
```bash
.\build_linux.ps1
```

### Embedded (Zephyr)
```bash
.\build_zephyr.ps1
```

## Conventions

- AT commands are sent as null-terminated strings over UART
- All UART operations must support both blocking and non-blocking modes
- Timeouts must be configurable for every AT command exchange
- Logging must be abstracted (use Zephyr logging on embedded, stdout/spdlog on desktop)

## Static Analysis

Install [Cppcheck](https://cppcheck.sourceforge.io/) and run:

```bash
cppcheck --project=cppcheck.cppcheck --std=c++17 --enable=warning,style,performance,portability,information
```

To generate a detailed checkers report:

### Desktop (Windows)
```bash
cppcheck --project=cppcheck-windows.cppcheck --std=c++17 --enable=warning,style,performance,portability,information --checkers-report=checkers.txt
```

### Embedded (Zephyr)
```bash
cppcheck --project=cppcheck-zephyr.cppcheck --std=c++17 --enable=warning,style,performance,portability,information
```

## Testing

- Unit tests use Google Test (desktop only)
- Mock the HAL interface for testability
- Integration tests run against real hardware or a modem simulator

```bash
cd build
ctest --output-on-failure
```

## Directory Structure

```
include/          — Public headers
src/              — Implementation files
src/hal/          — Hardware abstraction layer implementations
tests/            — Unit and integration tests
cmake/            — CMake modules and toolchain files
```
