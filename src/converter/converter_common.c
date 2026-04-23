/* converter_common.c
 * Platform-agnostic utility functions shared across all platforms.
 * These functions delegate platform-specific work to platform_*() calls
 * defined in converter_platform.h.
 */

#include "converter_common.h"
#include "converter_platform.h"
#include <string.h>
#include <stdio.h>

/** Returns the number of logical CPU cores (delegates to platform). */
int get_cpu_count(void) {
    return platform_get_cpu_count();
}

/** Returns half of CPU count, minimum 1, for FFmpeg filter threading. */
int get_filter_threads(void) {
    int cpus = get_cpu_count();
    int threads = cpus / 2;
    if (threads < 1) threads = 1;
    return threads;
}

/**
 * Parses an "HH:MM:SS.mmm" string into seconds as a double.
 * Returns 0.0 if parsing fails.
 */
double parse_time_hms(const char *s) {
    int h = 0, m = 0;
    double sec = 0.0;
    if (sscanf(s, "%d:%d:%lf", &h, &m, &sec) == 3)
        return h * 3600.0 + m * 60.0 + sec;
    return 0.0;
}

/**
 * Returns 1 if path is absolute on any platform, 0 if relative.
 * POSIX: starts with '/'
 * Windows: starts with drive letter + ':' or UNC '\\\\'
 */
int is_path_absolute(const char* path) {
    if (!path || path[0] == '\0') return 0;
    if (path[0] == '/') return 1;          /* POSIX */
#ifdef _WIN32
    size_t plen = strlen(path);
    /* Windows: "C:\..." or "\\server\..." */
    if (plen >= 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
        return 1;
    if (plen >= 2 && path[0] == '\\' && path[1] == '\\')
        return 1;
#endif
    return 0;
}
