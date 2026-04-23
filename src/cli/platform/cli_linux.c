/**
 * platform/cli_linux.c
 * Linux-specific CLI platform implementation.
 *
 * Implements the cli_platform.h interface for Linux using:
 *  - LinuxCodecSupport / linux_probe_codec_support() for codec detection
 *  - POSIX stat/access/mkdir for file operations
 *  - getenv("HOME") for home directory
 *  - mux_run_postprocess() for mkvmerge post-processing
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "converter.h"
#include "cli_platform.h"
#include "linux/runtime_probe.h"
#include "mux.h"

/* ---------------------------------------------------------------
 *  Private platform handle definition
 * --------------------------------------------------------------- */

/* Maximum codec entries: copy + prores + prores_ks + mux +
 *                        h264_vaapi + hevc_vaapi                 */
#define LINUX_MAX_CODECS 6

struct CliPlatformHandle {
    LinuxCodecSupport   support;
    PlatformCodecEntry  entries[LINUX_MAX_CODECS];
    int                 codec_count;
};

/* ---------------------------------------------------------------
 *  Lifecycle
 * --------------------------------------------------------------- */

CliPlatformHandle* cli_platform_init(void) {
    CliPlatformHandle* h;

    h = calloc(1, sizeof(*h));
    if (!h)
        return NULL;

    linux_probe_codec_support(&h->support);

    /* Build codec list */
    h->entries[h->codec_count].name          = "copy";
    h->entries[h->codec_count].needs_profile = 0;
    h->entries[h->codec_count].needs_deblock = 0;
    h->codec_count++;

    h->entries[h->codec_count].name          = "prores";
    h->entries[h->codec_count].needs_profile = 1;
    h->entries[h->codec_count].needs_deblock = 1;
    h->codec_count++;

    h->entries[h->codec_count].name          = "prores_ks";
    h->entries[h->codec_count].needs_profile = 1;
    h->entries[h->codec_count].needs_deblock = 1;
    h->codec_count++;

    h->entries[h->codec_count].name          = "mux";
    h->entries[h->codec_count].needs_profile = 0;
    h->entries[h->codec_count].needs_deblock = 0;
    h->codec_count++;

    if (h->support.has_h264_vaapi) {
        h->entries[h->codec_count].name          = "h264_vaapi";
        h->entries[h->codec_count].needs_profile = 0;
        h->entries[h->codec_count].needs_deblock = 0;
        h->codec_count++;
    }

    if (h->support.has_hevc_vaapi) {
        h->entries[h->codec_count].name          = "hevc_vaapi";
        h->entries[h->codec_count].needs_profile = 0;
        h->entries[h->codec_count].needs_deblock = 0;
        h->codec_count++;
    }

    return h;
}

void cli_platform_cleanup(CliPlatformHandle* h) {
    free(h);
}

/* ---------------------------------------------------------------
 *  Codec / audio-mode availability
 * --------------------------------------------------------------- */

int platform_codec_is_available(const CliPlatformHandle* h, const char* codec) {
    if (!codec)
        return 0;

    if (!strcmp(codec, "copy")     ||
        !strcmp(codec, "prores")   ||
        !strcmp(codec, "prores_ks")||
        !strcmp(codec, "mux"))
        return 1;

    if (!h)
        return 0;

    if (!strcmp(codec, "h264_vaapi"))
        return h->support.has_h264_vaapi;
    if (!strcmp(codec, "hevc_vaapi"))
        return h->support.has_hevc_vaapi;

    return 0;
}

int platform_audio_mode_is_available(const char* mode) {
    return mode &&
           (!strcmp(mode, "pcm")                 ||
            !strcmp(mode, "fdk_aac_q5")           ||
            !strcmp(mode, "fdk_aac_q5_ac3_640")   ||
            !strcmp(mode, "fdk_aac_q2")           ||
            !strcmp(mode, "fdk_aac_q2_ac3_640"));
}

int platform_mux_is_supported(void) {
    return 1;
}

/* ---------------------------------------------------------------
 *  Codec list
 * --------------------------------------------------------------- */

int platform_get_codec_count(const CliPlatformHandle* h) {
    return h ? h->codec_count : 0;
}

const PlatformCodecEntry* platform_get_codec_entries(const CliPlatformHandle* h) {
    return h ? h->entries : NULL;
}

/* ---------------------------------------------------------------
 *  Hardware device defaults
 * --------------------------------------------------------------- */

void platform_apply_hw_device(ConvertOptions* opts, const CliPlatformHandle* h) {
    if (!opts || !h)
        return;

    if ((!strcmp(opts->codec, "h264_vaapi") ||
         !strcmp(opts->codec, "hevc_vaapi")) &&
        h->support.default_render_node[0] != '\0') {
        strncpy(opts->hw_device, h->support.default_render_node,
                sizeof(opts->hw_device) - 1);
        opts->hw_device[sizeof(opts->hw_device) - 1] = '\0';
        opts->hwaccel_enabled = 1;
    }
}

/* ---------------------------------------------------------------
 *  Home directory
 * --------------------------------------------------------------- */

const char* cli_get_home_dir(void) {
    const char* home = getenv("HOME");
    return (home && home[0] != '\0') ? home : ".";
}

/* ---------------------------------------------------------------
 *  File / directory helpers
 * --------------------------------------------------------------- */

int platform_file_is_regular_readable(const char* path) {
    struct stat st;
    return path && path[0] != '\0' &&
           stat(path, &st) == 0 &&
           S_ISREG(st.st_mode) &&
           access(path, R_OK) == 0;
}

int platform_dir_is_writable(const char* path) {
    struct stat st;
    return path && path[0] != '\0' &&
           stat(path, &st) == 0 &&
           S_ISDIR(st.st_mode) &&
           access(path, W_OK) == 0;
}

int platform_ensure_output_dir(const char* path) {
    struct stat st;

    if (!path || path[0] == '\0')
        return 0;

    if (stat(path, &st) != 0) {
        if (errno == ENOENT) {
            if (mkdir(path, 0755) != 0) {
                perror("mkdir");
                return 0;
            }
        } else {
            perror("stat");
            return 0;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: '%s' exists but is not a directory.\n", path);
        return 0;
    }

    if (access(path, W_OK) != 0) {
        perror("access");
        return 0;
    }

    return 1;
}

/* ---------------------------------------------------------------
 *  Mux post-processing
 * --------------------------------------------------------------- */

ConverterError platform_run_mux_postprocess(const ConvertOptions* opts,
                                            const ConverterCallbacks* cb,
                                            const char* input_file)
{
    ConvertOptions file_opts;
    MuxOptions     mux_opts;
    char effective_output_dir[4096];

    if (!opts || !cb || !input_file)
        return ERR_INVALID_OPTIONS;

    memset(&file_opts, 0, sizeof(file_opts));
    file_opts = *opts;
    strcpy(file_opts.codec, "copy");

    if (opts->output_dir[0] != '\0') {
        strncpy(effective_output_dir, opts->output_dir,
                sizeof(effective_output_dir) - 1);
        effective_output_dir[sizeof(effective_output_dir) - 1] = '\0';
    } else {
        const char* home = cli_get_home_dir();
        snprintf(effective_output_dir, sizeof(effective_output_dir),
                 "%s/ffmpeg_converter", home);
    }

    strncpy(file_opts.output_dir, effective_output_dir,
            sizeof(file_opts.output_dir) - 1);
    file_opts.output_dir[sizeof(file_opts.output_dir) - 1] = '\0';

    memset(&mux_opts, 0, sizeof(mux_opts));
    converter_make_output_name(input_file, &file_opts,
                               mux_opts.intermediate_file,
                               sizeof(mux_opts.intermediate_file));
    strncpy(mux_opts.video_track_file, opts->video_track_path,
            sizeof(mux_opts.video_track_file) - 1);
    mux_opts.video_track_file[sizeof(mux_opts.video_track_file) - 1] = '\0';
    strncpy(mux_opts.output_file, mux_opts.intermediate_file,
            sizeof(mux_opts.output_file) - 1);
    mux_opts.output_file[sizeof(mux_opts.output_file) - 1] = '\0';
    mux_opts.overwrite = opts->overwrite;

    return mux_run_postprocess(&mux_opts, opts, cb);
}
