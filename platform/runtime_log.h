#pragma once

#include <stdio.h>

#if defined(NESCO_RUNTIME_LOGS)
#define NESCO_LOGF(...)      \
    do {                     \
        printf(__VA_ARGS__); \
        fflush(stdout);      \
    } while (0)
#define NESCO_LOG_PUTS(text) \
    do {                     \
        fputs((text), stdout); \
        fflush(stdout);      \
    } while (0)
#else
#define NESCO_LOGF(...) do { } while (0)
#define NESCO_LOG_PUTS(text) do { } while (0)
#endif

#if defined(NESCO_RUNTIME_LOGS)
#define NESCO_LOG_RUNTIME(...) NESCO_LOGF(__VA_ARGS__)
#define NESCO_RUNTIME_ONLY(stmt) do { stmt; } while (0)
#else
#define NESCO_LOG_RUNTIME(...) do { } while (0)
#define NESCO_RUNTIME_ONLY(stmt) do { } while (0)
#endif

#if defined(NESCO_CORE1_BASELINE_LOG)
#define NESCO_LOG_PERF(...)      \
    do {                         \
        printf(__VA_ARGS__);     \
    } while (0)
#define NESCO_PERF_ONLY(stmt) do { stmt; } while (0)
#else
#define NESCO_LOG_PERF(...) do { } while (0)
#define NESCO_PERF_ONLY(stmt) do { } while (0)
#endif
