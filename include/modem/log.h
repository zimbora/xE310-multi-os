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

#define MODEM_LOG_MODULE_REGISTER(name)
#define MODEM_LOG_MODULE_DECLARE(name)

#define MODEM_LOG_DBG(...) do { std::fprintf(stdout, "[DBG] " __VA_ARGS__); std::fprintf(stdout, "\n"); } while (0)
#define MODEM_LOG_INF(...) do { std::fprintf(stdout, "[INF] " __VA_ARGS__); std::fprintf(stdout, "\n"); } while (0)
#define MODEM_LOG_WRN(...) do { std::fprintf(stderr, "[WRN] " __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
#define MODEM_LOG_ERR(...) do { std::fprintf(stderr, "[ERR] " __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)

#endif
