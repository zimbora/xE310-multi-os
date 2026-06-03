#pragma once

#ifdef PLATFORM_ZEPHYR

#include <zephyr/logging/log.h>

#define MODEM_LOG_MODULE_REGISTER(name) LOG_MODULE_REGISTER(name, LOG_LEVEL_DBG)
#define MODEM_LOG_MODULE_DECLARE(name)  LOG_MODULE_DECLARE(name, LOG_LEVEL_DBG)

#define MODEM_LOG_DBG(...) LOG_DBG(__VA_ARGS__)
#define MODEM_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define MODEM_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define MODEM_LOG_ERR(...) LOG_ERR(__VA_ARGS__)

#else

#include <cstdio>
#include <ctime>
#include <chrono>

namespace modem_log_detail {
inline const char* timestamp() {
    using clock = std::chrono::system_clock;
    static char buf[24];
    auto now   = clock::now();
    auto ms    = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = clock::to_time_t(now);
    struct tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, (int)ms.count());
    return buf;
}
} // namespace modem_log_detail

#define MODEM_LOG_MODULE_REGISTER(name)
#define MODEM_LOG_MODULE_DECLARE(name)

#define MODEM_LOG_DBG(...) do { std::fprintf(stdout, "[%s][DBG] ", modem_log_detail::timestamp()); std::fprintf(stdout, __VA_ARGS__); std::fprintf(stdout, "\n"); } while (0)
#define MODEM_LOG_INF(...) do { std::fprintf(stdout, "[%s][INF] ", modem_log_detail::timestamp()); std::fprintf(stdout, __VA_ARGS__); std::fprintf(stdout, "\n"); } while (0)
#define MODEM_LOG_WRN(...) do { std::fprintf(stderr, "[%s][WRN] ", modem_log_detail::timestamp()); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
#define MODEM_LOG_ERR(...) do { std::fprintf(stderr, "[%s][ERR] ", modem_log_detail::timestamp()); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)

#endif
