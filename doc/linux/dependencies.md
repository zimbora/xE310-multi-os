# Linux Dependencies

## Toolchain

- GCC or Clang with C++17 support
- CMake >= 3.20
- Ninja or Make

## System Libraries

- POSIX API (libc) — serial port access:
  - open / close / read / write (fcntl.h, unistd.h)
  - termios (termios.h) — baud rate, data bits, stop bits, raw mode
  - select (sys/select.h) — read timeout support

## C++ Standard Library

- `<string>`
- `<memory>` (std::unique_ptr, std::make_unique)
- `<cstdint>`
- `<cstring>`
- `<cerrno>`
- `<algorithm>`
- `<iostream>` (main.cpp)

## Testing

- Google Test v1.14.0 (fetched automatically via CMake FetchContent)
- Google Mock (included with Google Test)

## Build Commands

```
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```
