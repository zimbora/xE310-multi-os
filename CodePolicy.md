# Code Policy

## Forbidden Dynamic Allocation

To prevent dynamic memory allocation in embedded and hot-path code, avoid the following in production code paths:

- `new`, `delete`, `new[]`, `delete[]`
- `std::make_unique`, `std::make_shared`, `std::allocate_shared`
- Containers that allocate dynamically by default, such as:
  - `std::vector`
  - `std::string`
  - `std::deque`
  - `std::list`
  - `std::map`, `std::unordered_map`
  - `std::set`, `std::unordered_set`

Use fixed-size or static-capacity alternatives where possible.

Enforcement command:

```bash
python3 scripts/check_dynamic_memory_policy.py
```

For justified exceptions at platform-factory boundaries, add an inline marker:

```cpp
// dynamic-memory-allow: <reason>
```

## Code Style

- C++17 standard (compatible with Zephyr's toolchain)
- Use `#pragma once` for include guards
- Keep headers minimal — forward-declare where possible
- No exceptions on embedded targets — use error codes or `std::expected`-style patterns
- No dynamic memory allocation in hot paths on embedded targets

## Naming Conventions

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

## Style Checker

Run the naming convention checker:

```bash
python3 scripts/check_code_style.py
```

With verbose output and summary:

```bash
python3 scripts/check_code_style.py --verbose --summary
```

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

Run the dynamic-memory policy check:

```bash
python3 scripts/check_dynamic_memory_policy.py
```

### clang-tidy

Run [clang-tidy](https://clang.llvm.org/extra/clang-tidy/) on all source files:

```bash
clang-tidy --extra-arg="-Iinclude" --extra-arg="-std=c++17" --warnings-as-errors="*" src/*.cpp
```

Run on a single file:

```bash
clang-tidy --extra-arg="-Iinclude" --extra-arg="-std=c++17" src/xe310.cpp
```

On Linux with Ninja, you can use a compile commands database instead:

```bash
cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build src/*.cpp
```

### Cppcheck

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
