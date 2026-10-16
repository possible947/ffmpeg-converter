/* converter_common.h
 * Declarations for platform-agnostic utility functions shared across all
 * platforms. These functions do not depend on platform-specific APIs.
 */

#ifndef CONVERTER_COMMON_H
#define CONVERTER_COMMON_H

#include "converter.h"
#include "mux_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Returns the number of logical CPU cores (delegates to platform). */
int    get_cpu_count(void);

/** Returns half of CPU count, minimum 1, for FFmpeg filter threading. */
int    get_filter_threads(void);

/**
 * Returns 1 if path is absolute on any platform, 0 if relative.
 * POSIX: starts with '/'
 */
int    is_path_absolute(const char* path);

/* Source codec may be unknown until the input is probed. */
AudioModeAvailability converter_audio_mode_availability(
    const ConvertOptions *opts, MuxVideoCodec source_codec);

#ifdef __cplusplus
}
#endif

#endif /* CONVERTER_COMMON_H */
