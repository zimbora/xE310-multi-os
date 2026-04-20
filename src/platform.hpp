#pragma once

/*
 * platform.hpp
 *
 * Cross-platform abstraction macros for the xE310 library.
 * Supports: Arduino/ESP32, Linux/macOS (POSIX), Windows
 */

// ── Platform detection ────────────────────────────────────────────────────────

#if defined(ARDUINO)
  #define XE310_PLATFORM_ARDUINO
  #include <Arduino.h>
  #define XE310_DELAY(ms)   delay(ms)
  #define XE310_MILLIS()    millis()
  // String type: Arduino String
  // (no extra typedef needed)

#elif defined(_WIN32) || defined(_WIN64)
  #define XE310_PLATFORM_WINDOWS
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <stdint.h>
  #include <string>
  #define XE310_DELAY(ms)   Sleep(ms)
  inline uint32_t _xe310_millis() {
    return (uint32_t)GetTickCount();
  }
  #define XE310_MILLIS()    _xe310_millis()
  // Use std::string as the String type on desktop
  using String = std::string;

#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
  #define XE310_PLATFORM_POSIX
  #include <unistd.h>
  #include <chrono>
  #include <string>
  #define XE310_DELAY(ms)   usleep((ms) * 1000)
  inline uint32_t _xe310_millis() {
    static auto _start = std::chrono::steady_clock::now();
    auto _now = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(_now - _start).count();
  }
  #define XE310_MILLIS()    _xe310_millis()
  // Use std::string as the String type on desktop
  using String = std::string;

#else
  #error "Unsupported platform – define ARDUINO, _WIN32/_WIN64, or compile on a POSIX system."
#endif
