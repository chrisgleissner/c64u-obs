#ifndef C64U_LOGGING_H
#define C64U_LOGGING_H

#include <obs-module.h>
#include <time.h>
#include <stdint.h>

#ifdef _WIN32
// Prevent winsock.h inclusion to avoid conflicts with winsock2.h
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_ // Prevent winsock.h from being included
#endif
// Include winsock2.h before windows.h to prevent warnings
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// Windows compatibility shims
#ifndef snprintf
#define snprintf _snprintf
#endif

// Windows compatibility for clock_gettime (if needed elsewhere)
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
static inline int clock_gettime(int clk_id, struct timespec *ts)
{
    (void)clk_id; // Unused parameter
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    // Convert from 100-ns intervals since 1601 to seconds/nanoseconds since 1970
    t -= 116444736000000000ULL;
    ts->tv_sec = t / 10000000ULL;
    ts->tv_nsec = (t % 10000000ULL) * 100;
    return 0;
}
#endif

#else
#include <sys/time.h>
#endif

// Logging control - using extern to avoid multiple definitions
extern bool c64u_debug_logging;

// Fast milliseconds since epoch for logging
static inline uint64_t c64u_get_millis(void)
{
#ifdef _WIN32
    // Windows: Use GetSystemTimeAsFileTime for millisecond precision
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    // Convert Windows FILETIME to milliseconds since Unix epoch
    uint64_t ticks = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    // Windows epoch starts Jan 1, 1601. Unix epoch starts Jan 1, 1970.
    // Difference is 11644473600 seconds = 116444736000000000 * 100ns ticks
    uint64_t unix_ticks = ticks - 116444736000000000ULL;
    return unix_ticks / 10000ULL; // Convert 100ns ticks to milliseconds
#else
    // POSIX: Use clock_gettime
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
#endif
}

#define C64U_LOG_INFO(format, ...)                                                                        \
    do {                                                                                                   \
        blog(LOG_INFO, "[%llu] " format, (unsigned long long)c64u_get_millis(), ##__VA_ARGS__);          \
    } while (0)

#define C64U_LOG_DEBUG(format, ...)                                                                       \
    do {                                                                                                   \
        if (c64u_debug_logging) {                                                                          \
            blog(LOG_DEBUG, "[C64U %llu] " format, (unsigned long long)c64u_get_millis(), ##__VA_ARGS__); \
        }                                                                                                  \
    } while (0)

#define C64U_LOG_WARNING(format, ...)                                                                     \
    do {                                                                                                   \
        blog(LOG_WARNING, "[%llu] " format, (unsigned long long)c64u_get_millis(), ##__VA_ARGS__);       \
    } while (0)

#define C64U_LOG_ERROR(format, ...)                                                                       \
    do {                                                                                                   \
        blog(LOG_ERROR, "[%llu] " format, (unsigned long long)c64u_get_millis(), ##__VA_ARGS__);         \
    } while (0)

#endif // C64U_LOGGING_H
