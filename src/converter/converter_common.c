/* converter_common.c
 * Platform-agnostic utility functions shared across all platforms.
 * These functions delegate platform-specific work to platform_*() calls
 * defined in converter_platform.h.
 */

#include "converter_common.h"
#include "converter_platform.h"
#include <string.h>
#include <stdio.h>

AudioModeAvailability converter_audio_mode_availability(
    const ConvertOptions *opts, MuxVideoCodec source_codec)
{
    MuxBuildProfile profile;
    AudioModeAvailability empty = {{0}, 0};
    AudioWorkflow workflow = AUDIO_WORKFLOW_SOFTWARE;
    MuxMode mode = MUX_MODE_INVALID;
    MuxVideoCodec replacement = MUX_VIDEO_UNKNOWN;
    if (!opts || !mux_build_profile_default(&profile))
        return empty;
    if (!strcmp(opts->codec, "hq_converter")) {
        workflow = AUDIO_WORKFLOW_HQ;
        replacement = mux_video_codec_parse(opts->hw_device);
    } else if (!strcmp(opts->codec, "mux") || !strcmp(opts->codec, "copy") ||
               !strcmp(opts->codec, "m4v")) {
        workflow = AUDIO_WORKFLOW_MUX;
        if (!strcmp(opts->codec, "m4v"))
            mode = MUX_MODE_M4V;
        else if (!strcmp(opts->codec, "copy") &&
                 (!opts->preset[0] || !strcmp(opts->preset, "default")))
            mode = MUX_MODE_COPY;
        else
            mode = mux_mode_parse(opts->preset[0] ? opts->preset : "mkv");
    }
    return audio_mode_availability(&profile, workflow, mode,
                                    &source_codec, 1, replacement);
}

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
 */
int is_path_absolute(const char* path) {
    if (!path || path[0] == '\0') return 0;
    if (path[0] == '/') return 1;
    return 0;
}
