/**
 * platform/cli_windows.c
 * Windows-specific CLI platform implementation.
 *
 * Implements the cli_platform.h interface for Windows using:
 *  - WindowsCodecSupport / windows_probe_codec_support() for codec detection
 *  - Win32 / CRT file APIs (_stat, _mkdir, _access) for file operations
 *  - USERPROFILE / HOMEPATH environment variables for home directory
 *
 * Codec support: copy, prores (software), prores_ks (software),
 *                h264_nvenc / hevc_nvenc (NVIDIA, if available),
 *                h264_amf / hevc_amf (AMD, if available),
 *                h264_qsv / hevc_qsv (Intel, if available)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <windows.h>

/* Use Windows CRT stat/access/mkdir compatible with MSVC */
#ifdef _WIN32
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <io.h>       /* _access */
#  include <direct.h>   /* _mkdir  */
#  define cli_stat    _stat
#  define cli_statbuf struct _stat
#  define cli_access  _access
#  define cli_mkdir(p) _mkdir(p)
#  define S_ISREG(m)  (((m) & _S_IFMT) == _S_IFREG)
#  define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#  define R_OK 4  /* read permission — matches Windows CRT _access() constant */
#  define W_OK 2  /* write permission — matches Windows CRT _access() constant */
#else
/* Fallback for non-Windows targets (should not happen for this file) */
#  include <sys/stat.h>
#  include <unistd.h>
#  define cli_stat    stat
#  define cli_statbuf struct stat
#  define cli_access  access
#  define cli_mkdir(p) mkdir(p, 0755)
#endif

#include "converter.h"
#include "cli_platform.h"
#include "windows/runtime_probe.h"

/* ---------------------------------------------------------------
 *  Private platform handle definition
 * --------------------------------------------------------------- */

/* Maximum codec entries: copy + prores + prores_ks +
 *                        h264_nvenc + hevc_nvenc +
 *                        h264_amf  + hevc_amf  +
 *                        h264_qsv  + hevc_qsv           */
#define WINDOWS_MAX_CODECS 9

struct CliPlatformHandle {
    WindowsCodecSupport support;
    PlatformCodecEntry  entries[WINDOWS_MAX_CODECS];
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

    windows_probe_codec_support(&h->support);

    /* Always-available codecs */
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

    /* NVIDIA NVENC */
    if (h->support.has_h264_nvenc) {
        h->entries[h->codec_count].name          = "h264_nvenc";
        h->entries[h->codec_count].needs_profile = 0;
        h->entries[h->codec_count].needs_deblock = 0;
        h->codec_count++;
    }
    if (h->support.has_hevc_nvenc) {
        h->entries[h->codec_count].name          = "hevc_nvenc";
        h->entries[h->codec_count].needs_profile = 0;
        h->entries[h->codec_count].needs_deblock = 0;
        h->codec_count++;
    }

    /* AMD AMF */
    if (h->support.has_h264_amf) {
        h->entries[h->codec_count].name          = "h264_amf";
        h->entries[h->codec_count].needs_profile = 0;
        h->entries[h->codec_count].needs_deblock = 0;
        h->codec_count++;
    }
    if (h->support.has_hevc_amf) {
        h->entries[h->codec_count].name          = "hevc_amf";
        h->entries[h->codec_count].needs_profile = 0;
        h->entries[h->codec_count].needs_deblock = 0;
        h->codec_count++;
    }

    /* Intel QSV */
    if (h->support.has_h264_qsv) {
        h->entries[h->codec_count].name          = "h264_qsv";
        h->entries[h->codec_count].needs_profile = 0;
        h->entries[h->codec_count].needs_deblock = 0;
        h->codec_count++;
    }
    if (h->support.has_hevc_qsv) {
        h->entries[h->codec_count].name          = "hevc_qsv";
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

    /* Software codecs always available */
    if (!strcmp(codec, "copy")     ||
        !strcmp(codec, "prores")   ||
        !strcmp(codec, "prores_ks"))
        return 1;

    if (!h)
        return 0;

    if (!strcmp(codec, "h264_nvenc")) return h->support.has_h264_nvenc;
    if (!strcmp(codec, "hevc_nvenc")) return h->support.has_hevc_nvenc;
    if (!strcmp(codec, "h264_amf"))   return h->support.has_h264_amf;
    if (!strcmp(codec, "hevc_amf"))   return h->support.has_hevc_amf;
    if (!strcmp(codec, "h264_qsv"))   return h->support.has_h264_qsv;
    if (!strcmp(codec, "hevc_qsv"))   return h->support.has_hevc_qsv;

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
    /* NVENC/AMF/QSV do not require a device path in ffmpeg */
    (void)opts;
    (void)h;
}

/* ---------------------------------------------------------------
 *  Home directory
 * --------------------------------------------------------------- */

const char* cli_get_home_dir(void) {
    const char* home;

    /* Standard Windows user profile variables */
    home = getenv("USERPROFILE");
    if (home && home[0] != '\0')
        return home;

    home = getenv("HOMEPATH");
    if (home && home[0] != '\0')
        return home;

    return ".";
}

/* ---------------------------------------------------------------
 *  File / directory helpers
 * --------------------------------------------------------------- */

int platform_file_is_regular_readable(const char* path) {
    wchar_t wpath[2048];
    int wlen;
    DWORD attrs;

    if (!path || path[0] == '\0')
        return 0;

    /* Prefer UTF-8; fallback to active ANSI code page. */
    wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                               wpath, (int)(sizeof(wpath) / sizeof(wpath[0])));
    if (wlen == 0) {
        wlen = MultiByteToWideChar(CP_ACP, 0, path, -1,
                                   wpath, (int)(sizeof(wpath) / sizeof(wpath[0])));
    }
    if (wlen == 0)
        return 0;

    attrs = GetFileAttributesW(wpath);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return 0;

    return (_waccess(wpath, R_OK) == 0) ? 1 : 0;
}

int platform_dir_is_writable(const char* path) {
    wchar_t wpath[2048];
    int wlen;
    DWORD attrs;

    if (!path || path[0] == '\0')
        return 0;

    wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                               wpath, (int)(sizeof(wpath) / sizeof(wpath[0])));
    if (wlen == 0) {
        wlen = MultiByteToWideChar(CP_ACP, 0, path, -1,
                                   wpath, (int)(sizeof(wpath) / sizeof(wpath[0])));
    }
    if (wlen == 0)
        return 0;

    attrs = GetFileAttributesW(wpath);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
        return 0;

    return (_waccess(wpath, W_OK) == 0) ? 1 : 0;
}

int platform_ensure_output_dir(const char* path) {
    cli_statbuf st;

    if (!path || path[0] == '\0')
        return 0;

    if (cli_stat(path, &st) != 0) {
        if (errno == ENOENT) {
            if (cli_mkdir(path) != 0) {
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

    if (cli_access(path, W_OK) != 0) {
        perror("access");
        return 0;
    }

    return 1;
}

/* ---------------------------------------------------------------
 *  Mux post-processing (not supported on Windows)
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
