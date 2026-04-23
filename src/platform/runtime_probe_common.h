/**
 * runtime_probe_common.h
 * Platform-agnostic helpers and shared data structures for runtime probing.
 * No #ifdef or platform-specific code.
 */

#ifndef RUNTIME_PROBE_COMMON_H
#define RUNTIME_PROBE_COMMON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Common binary resolution fields (same on all platforms).
 * Windows and macOS CodecSupport structs embed this as their first member. */
typedef struct {
    char ffmpeg_bin[1024];
    char ffprobe_bin[1024];
    char mkvmerge_bin[1024];
    char mp4box_bin[1024];
    int  using_bundled_ffmpeg;
    int  using_bundled_ffprobe;
    int  using_bundled_mkvmerge;
    int  using_bundled_mp4box;
} RuntimeProbeBinaries;

/* Portable helper functions implemented in runtime_probe_common.c */
void copy_string(char *dst, size_t dst_sz, const char *src);
int  starts_with(const char *text, const char *prefix);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_PROBE_COMMON_H */
