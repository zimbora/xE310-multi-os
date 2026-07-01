#pragma once

// Compile-time log levels
// 0: none, 1: error, 2: warning, 3: info, 4: debug
#define MODEM_LOG_LEVEL_NONE 0
#define MODEM_LOG_LEVEL_ERR 1
#define MODEM_LOG_LEVEL_WRN 2
#define MODEM_LOG_LEVEL_INF 3
#define MODEM_LOG_LEVEL_DBG 4

// Default levels can be overridden via compiler definitions, e.g.:
// -DMODEM_LOG_LEVEL=4 -DNETWORK_LOG_LEVEL=2
#ifndef MODEM_LOG_LEVEL
#define MODEM_LOG_LEVEL MODEM_LOG_LEVEL_INF
#endif

#ifndef NETWORK_LOG_LEVEL
#define NETWORK_LOG_LEVEL MODEM_LOG_LEVEL
#endif

#if defined(PLATFORM_ZEPHYR) || defined(__ZEPHYR__)

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <cstdio>

namespace modem_log_detail {
inline const char* timestamp() {
    static char s_buf[24];
    int64_t uptime_ms = k_uptime_get();
    uint32_t ms = static_cast<uint32_t>(uptime_ms % 1000);
    uint32_t total_seconds = static_cast<uint32_t>(uptime_ms / 1000);
    uint32_t sec = total_seconds % 60;
    total_seconds /= 60;
    uint32_t min = total_seconds % 60;
    total_seconds /= 60;
    uint32_t hour = total_seconds % 24;
    std::snprintf(s_buf, sizeof(s_buf), "%02u:%02u:%02u.%03u", static_cast<unsigned>(hour), static_cast<unsigned>(min),
                  static_cast<unsigned>(sec), static_cast<unsigned>(ms));
    return s_buf;
}
} // namespace modem_log_detail

#define MODEM_LOG_MODULE_REGISTER(name)
#define MODEM_LOG_MODULE_DECLARE(name)

#if MODEM_LOG_LEVEL >= MODEM_LOG_LEVEL_DBG
#define MODEM_LOG_DBG(...)                                                                                             \
    do {                                                                                                               \
        printk("[%s][DBG][M] ", modem_log_detail::timestamp());                                                        \
        printk(__VA_ARGS__);                                                                                           \
        printk("\n");                                                                                                  \
    } while (0)
#else
#define MODEM_LOG_DBG(...)                                                                                             \
    do {                                                                                                               \
    } while (0)
#endif

#if MODEM_LOG_LEVEL >= MODEM_LOG_LEVEL_INF
#define MODEM_LOG_INF(...)                                                                                             \
    do {                                                                                                               \
        printk("[%s][INF][M] ", modem_log_detail::timestamp());                                                        \
        printk(__VA_ARGS__);                                                                                           \
        printk("\n");                                                                                                  \
    } while (0)
#else
#define MODEM_LOG_INF(...)                                                                                             \
    do {                                                                                                               \
    } while (0)
#endif

#if MODEM_LOG_LEVEL >= MODEM_LOG_LEVEL_WRN
#define MODEM_LOG_WRN(...)                                                                                             \
    do {                                                                                                               \
        printk("[%s][WRN][M] ", modem_log_detail::timestamp());                                                        \
        printk(__VA_ARGS__);                                                                                           \
        printk("\n");                                                                                                  \
    } while (0)
#else
#define MODEM_LOG_WRN(...)                                                                                             \
    do {                                                                                                               \
    } while (0)
#endif

#if MODEM_LOG_LEVEL >= MODEM_LOG_LEVEL_ERR
#define MODEM_LOG_ERR(...)                                                                                             \
    do {                                                                                                               \
        printk("[%s][ERR][M] ", modem_log_detail::timestamp());                                                        \
        printk(__VA_ARGS__);                                                                                           \
        printk("\n");                                                                                                  \
    } while (0)
#else
#define MODEM_LOG_ERR(...)                                                                                             \
    do {                                                                                                               \
    } while (0)
#endif

#if NETWORK_LOG_LEVEL >= MODEM_LOG_LEVEL_DBG
#define NETWORK_LOG_DBG(...)                                                                                           \
    do {                                                                                                               \
        printk("[%s][DBG][N] ", modem_log_detail::timestamp());                                                        \
        printk(__VA_ARGS__);                                                                                           \
        printk("\n");                                                                                                  \
    } while (0)
#else
#define NETWORK_LOG_DBG(...)                                                                                           \
    do {                                                                                                               \
    } while (0)
#endif

#if NETWORK_LOG_LEVEL >= MODEM_LOG_LEVEL_INF
#define NETWORK_LOG_INF(...)                                                                                           \
    do {                                                                                                               \
        printk("[%s][INF][N] ", modem_log_detail::timestamp());                                                        \
        printk(__VA_ARGS__);                                                                                           \
        printk("\n");                                                                                                  \
    } while (0)
#else
#define NETWORK_LOG_INF(...)                                                                                           \
    do {                                                                                                               \
    } while (0)
#endif

#if NETWORK_LOG_LEVEL >= MODEM_LOG_LEVEL_WRN
#define NETWORK_LOG_WRN(...)                                                                                           \
    do {                                                                                                               \
        printk("[%s][WRN][N] ", modem_log_detail::timestamp());                                                        \
        printk(__VA_ARGS__);                                                                                           \
        printk("\n");                                                                                                  \
    } while (0)
#else
#define NETWORK_LOG_WRN(...)                                                                                           \
    do {                                                                                                               \
    } while (0)
#endif

#if NETWORK_LOG_LEVEL >= MODEM_LOG_LEVEL_ERR
#define NETWORK_LOG_ERR(...)                                                                                           \
    do {                                                                                                               \
        printk("[%s][ERR][N] ", modem_log_detail::timestamp());                                                        \
        printk(__VA_ARGS__);                                                                                           \
        printk("\n");                                                                                                  \
    } while (0)
#else
#define NETWORK_LOG_ERR(...)                                                                                           \
    do {                                                                                                               \
    } while (0)
#endif

#else

#include <cstdio>
#include <ctime>
#include <chrono>

namespace modem_log_detail {
inline const char* timestamp() {
    using clock = std::chrono::system_clock;
    static char s_buf[24];
    auto now = clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = clock::to_time_t(now);
    struct tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::snprintf(s_buf, sizeof(s_buf), "%02d:%02d:%02d.%03d", tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                  (int)ms.count());
    return s_buf;
}
} // namespace modem_log_detail

#define MODEM_LOG_MODULE_REGISTER(name)
#define MODEM_LOG_MODULE_DECLARE(name)

#if MODEM_LOG_LEVEL >= MODEM_LOG_LEVEL_DBG
#define MODEM_LOG_DBG(...)                                                                                             \
    do {                                                                                                               \
        std::fprintf(stdout, "[%s][DBG][M] ", modem_log_detail::timestamp());                                          \
        std::fprintf(stdout, __VA_ARGS__);                                                                             \
        std::fprintf(stdout, "\n");                                                                                    \
    } while (0)
#else
#define MODEM_LOG_DBG(...)                                                                                             \
    do {                                                                                                               \
    } while (0)
#endif

#if MODEM_LOG_LEVEL >= MODEM_LOG_LEVEL_INF
#define MODEM_LOG_INF(...)                                                                                             \
    do {                                                                                                               \
        std::fprintf(stdout, "[%s][INF][M] ", modem_log_detail::timestamp());                                          \
        std::fprintf(stdout, __VA_ARGS__);                                                                             \
        std::fprintf(stdout, "\n");                                                                                    \
    } while (0)
#else
#define MODEM_LOG_INF(...)                                                                                             \
    do {                                                                                                               \
    } while (0)
#endif

#if MODEM_LOG_LEVEL >= MODEM_LOG_LEVEL_WRN
#define MODEM_LOG_WRN(...)                                                                                             \
    do {                                                                                                               \
        std::fprintf(stderr, "[%s][WRN][M] ", modem_log_detail::timestamp());                                          \
        std::fprintf(stderr, __VA_ARGS__);                                                                             \
        std::fprintf(stderr, "\n");                                                                                    \
    } while (0)
#else
#define MODEM_LOG_WRN(...)                                                                                             \
    do {                                                                                                               \
    } while (0)
#endif

#if MODEM_LOG_LEVEL >= MODEM_LOG_LEVEL_ERR
#define MODEM_LOG_ERR(...)                                                                                             \
    do {                                                                                                               \
        std::fprintf(stderr, "[%s][ERR][M] ", modem_log_detail::timestamp());                                          \
        std::fprintf(stderr, __VA_ARGS__);                                                                             \
        std::fprintf(stderr, "\n");                                                                                    \
    } while (0)
#else
#define MODEM_LOG_ERR(...)                                                                                             \
    do {                                                                                                               \
    } while (0)
#endif

#if NETWORK_LOG_LEVEL >= MODEM_LOG_LEVEL_DBG
#define NETWORK_LOG_DBG(...)                                                                                           \
    do {                                                                                                               \
        std::fprintf(stdout, "[%s][DBG][N] ", modem_log_detail::timestamp());                                          \
        std::fprintf(stdout, __VA_ARGS__);                                                                             \
        std::fprintf(stdout, "\n");                                                                                    \
    } while (0)
#else
#define NETWORK_LOG_DBG(...)                                                                                           \
    do {                                                                                                               \
    } while (0)
#endif

#if NETWORK_LOG_LEVEL >= MODEM_LOG_LEVEL_INF
#define NETWORK_LOG_INF(...)                                                                                           \
    do {                                                                                                               \
        std::fprintf(stdout, "[%s][INF][N] ", modem_log_detail::timestamp());                                          \
        std::fprintf(stdout, __VA_ARGS__);                                                                             \
        std::fprintf(stdout, "\n");                                                                                    \
    } while (0)
#else
#define NETWORK_LOG_INF(...)                                                                                           \
    do {                                                                                                               \
    } while (0)
#endif

#if NETWORK_LOG_LEVEL >= MODEM_LOG_LEVEL_WRN
#define NETWORK_LOG_WRN(...)                                                                                           \
    do {                                                                                                               \
        std::fprintf(stderr, "[%s][WRN][N] ", modem_log_detail::timestamp());                                          \
        std::fprintf(stderr, __VA_ARGS__);                                                                             \
        std::fprintf(stderr, "\n");                                                                                    \
    } while (0)
#else
#define NETWORK_LOG_WRN(...)                                                                                           \
    do {                                                                                                               \
    } while (0)
#endif

#if NETWORK_LOG_LEVEL >= MODEM_LOG_LEVEL_ERR
#define NETWORK_LOG_ERR(...)                                                                                           \
    do {                                                                                                               \
        std::fprintf(stderr, "[%s][ERR][N] ", modem_log_detail::timestamp());                                          \
        std::fprintf(stderr, __VA_ARGS__);                                                                             \
        std::fprintf(stderr, "\n");                                                                                    \
    } while (0)
#else
#define NETWORK_LOG_ERR(...)                                                                                           \
    do {                                                                                                               \
    } while (0)
#endif

#endif
