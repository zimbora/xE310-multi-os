# Windows Dependencies

## Toolchain

- Visual Studio 2022 (MSVC v14.x) or MinGW/GCC with C++17 support
- CMake >= 3.20
- Ninja (optional, for Ninja generator)

## System Libraries

- Win32 API (kernel32.lib) — serial port access:
  - CreateFileA / CloseHandle
  - ReadFile / WriteFile
  - GetCommState / SetCommState (DCB)
  - GetCommTimeouts / SetCommTimeouts (COMMTIMEOUTS)
  - PurgeComm

## C++ Standard Library

- `<string>`
- `<memory>` (std::unique_ptr, std::make_unique)
- `<cstdint>`
- `<algorithm>`
- `<iostream>` (main.cpp)

## Testing

- Google Test v1.14.0 (fetched automatically via CMake FetchContent)
- Google Mock (included with Google Test)

## Build Commands

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```
