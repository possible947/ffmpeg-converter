/**
 * platform/cli_macos.c
 * macOS-specific CLI platform implementation.
 *
 * Implements the cli_platform.h interface for macOS using:
 *  - MacosCodecSupport / macos_probe_codec_support() for codec detection
 *  - POSIX stat/access/mkdir for file operations
 *  - getenv("HOME") for home directory
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
#include "macos/runtime_probe.h"

/* ---------------------------------------------------------------
 *  Private platform handle definition
 * --------------------------------------------------------------- */

/* Maximum codec entries: copy + prores + prores_ks +
 *                        prores_videotoolbox + hevc_videotoolbox  */
#define MACOS_MAX_CODECS 5

struct CliPlatformHandle {
    MacosCodecSupport   support;
    PlatformCodecEntry  entries[MACOS_MAX_CODECS];
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

    macos_probe_codec_support(&h->support);

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

    /* prores_videotoolbox: has profile selection but no deblock */
    h->entries[h->codec_count].name          = "prores_videotoolbox";
    h->entries[h->codec_count].needs_profile = 1;
    h->entries[h->codec_count].needs_deblock = 0;
    h->codec_count++;

    h->entries[h->codec_count].name          = "hevc_videotoolbox";
    h->entries[h->codec_count].needs_profile = 0;
    h->entries[h->codec_count].needs_deblock = 0;
    h->codec_count++;

    return h;
}

void cli_platform_cleanup(CliPlatformHandle* h) {
    free(h);
}

/* ---------------------------------------------------------------
 *  Codec / audio-mode availability
 * --------------------------------------------------------------- */

int platform_codec_is_available(const CliPlatformHandle* h, const char* codec) {
    (void)h;

    if (!codec)
        return 0;

    return !strcmp(codec, "copy")                 ||
           !strcmp(codec, "prores")               ||
           !strcmp(codec, "prores_ks")            ||
           !strcmp(codec, "prores_videotoolbox")  ||
           !strcmp(codec, "hevc_videotoolbox");
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
    return 0;
}

int platform_m4v_is_supported(void) {
    return 0;
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
    /* VideoToolbox does not require a device path */
    (void)opts;
    (void)h;
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
 *  Mux post-processing (not supported on macOS)
 * --------------------------------------------------------------- */

ConverterError platform_run_mux_postprocess(const ConvertOptions* opts,
                                            const ConverterCallbacks* cb,
                                            const char* input_file)
{
    (void)opts;
    (void)cb;
    (void)input_file;
    return ERR_INVALID_OPTIONS;
}

char** platform_utf8_argv(int argc, char** argv, int* needs_free)
{
    (void)argc;
    *needs_free = 0;
    return argv;
}
