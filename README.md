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

Thread communication responsibilities and event signaling are documented in [ThreadComms.md](ThreadComms.md).

## Code Style

Code style, naming conventions, formatting, and static analysis rules are documented in [CodePolicy.md](CodePolicy.md).

## Build System

- **CMake** as the primary build system
- Zephyr builds integrate via Zephyr's CMake system
- Desktop builds use standard CMake workflows

Build commands and log-level build examples are documented in [Build.md](Build.md).

### Windows Build Script Options

Use `build_vs.ps1` for Visual Studio builds:

```powershell
.\build_vs.ps1 [options] [extra CMake args]
```

Supported options:

- `-Config <Debug|Release|RelWithDebInfo|MinSizeRel>`: select the build configuration (default: `Release`).
- `-Clean`: remove previous `build/` output and in-source CMake artifacts before configuring.
- `-WithTests`: compile test targets (`MODEM_BUILD_TESTS=ON`).
- `-Test`: run `ctest` after build (also enables test compilation automatically).
- `-NoTests`: force-disable test compilation (`MODEM_BUILD_TESTS=OFF`), even if `-WithTests` or `-Test` was provided.
- `-D<VAR>=<VALUE>`: forward CMake definitions.

Log-level definitions:

- Preferred CMake variables: `-DMODEM_LOG_LEVEL=<0..4>` and `-DNETWORK_LOG_LEVEL=<0..4>`
- Compatibility aliases also accepted: `-DMODEM_MODEM_LOG=<0..4>` and `-DMODEM_NETWORK_LOG=<0..4>`

Examples:

```powershell
# Default build (tests are not compiled)
.\build_vs.ps1

# Build and run tests
.\build_vs.ps1 -WithTests -Test

# Debug logging levels
.\build_vs.ps1 -DMODEM_LOG_LEVEL=4 -DNETWORK_LOG_LEVEL=4
```

### CMake Compile Options

These options can be passed directly to CMake with `-D<VAR>=<VALUE>`:

- `MODEM_BUILD_TESTS` (`OFF` by default): enable with `ON` to compile desktop tests.
- `MODEM_BUILD_SHARED` (`OFF` by default): build shared library instead of static.
- `MODEM_LOG_LEVEL` (`3` default): modem logging level (`0..4`).
- `NETWORK_LOG_LEVEL` (`3` default): network logging level (`0..4`).

## Contributions

Contribution workflow, commit message rules, changelog generation, and validation commands are documented in [Contributions.md](Contributions.md).

## Conventions

- AT commands are sent as null-terminated strings over UART
- All UART operations must support both blocking and non-blocking modes
- Timeouts must be configurable for every AT command exchange
- Logging must be abstracted (use Zephyr logging on embedded, stdout/spdlog on desktop)

For thread request/response, socket data queueing, and event-flag signaling guidance, see [ThreadComms.md](ThreadComms.md).

## Code Formatting

See [CodePolicy.md](CodePolicy.md) for code formatting rules and commands.

## Static Analysis

See [CodePolicy.md](CodePolicy.md) for clang-format, clang-tidy, and cppcheck usage.

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
