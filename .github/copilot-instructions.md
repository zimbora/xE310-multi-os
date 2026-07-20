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

## Code Policy

- Follow [CodePolicy.md](../CodePolicy.md) for coding rules, style, and static-analysis requirements.

## Build System

- **CMake** as the primary build system
- Zephyr builds integrate via Zephyr's CMake system
- Desktop builds use standard CMake workflows

## Conventional Commits

This project follows the [Conventional Commits](https://www.conventionalcommits.org/) specification.

See [Contributions.md](../Contributions.md) for commit message rules, allowed types, and contribution workflow.

## Conventions

- AT commands are sent as null-terminated strings over UART
- All UART operations must support both blocking and non-blocking modes
- Timeouts must be configurable for every AT command exchange
- Logging must be abstracted (use Zephyr logging on embedded, stdout/spdlog on desktop)

Thread communication roles for `message_channel`, `message_queue`, and event flags are documented in [ThreadComms.md](../ThreadComms.md).

## Testing

- Unit tests use Google Test (desktop only)
- Mock the HAL interface for testability
- Integration tests run against real hardware or a modem simulator

## Validation Checklist

- Before considering a task complete, run static analysis checks when code changes affect C++ sources or headers.
- Required checks:
  - `python3 scripts/check_dynamic_memory_policy.py`
  - `clang-format --dry-run --Werror src/*.cpp src/hal/*.cpp include/modem/*.h include/modem/hal/*.h`
  - `clang-tidy --extra-arg="-Iinclude" --extra-arg="-std=c++17" --warnings-as-errors="*" src/*.cpp`
  - `cppcheck --enable=all --inline-suppr --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=normalCheckLevelMaxBranches --suppress=checkersReport --error-exitcode=1 --std=c++17 -I include src/ include/`
- If any check cannot be run, explicitly state why and what remains unvalidated.

## Directory Structure

```
include/          — Public headers
src/              — Implementation files
src/hal/          — Hardware abstraction layer implementations
tests/            — Unit and integration tests
cmake/            — CMake modules and toolchain files
```
