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
