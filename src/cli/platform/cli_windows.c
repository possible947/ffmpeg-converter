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
 *                h264_qsv / hevc_qsv (Intel, if available),
 *                prores_ks_vulkan (any GPU with Vulkan 1.1+, if available)
 *                mux (mkvmerge post-process; requires mkvmerge on PATH or
 *                     next to the executable)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <windows.h>
#include <shellapi.h>   /* CommandLineToArgvW */

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
#include "converter_platform.h"
#include "cli_platform.h"
#include "windows/runtime_probe.h"
#include "mux.h"

/* ---------------------------------------------------------------
 *  Private platform handle definition
 * --------------------------------------------------------------- */

/* Maximum codec entries: copy + prores + prores_ks +
 *                        h264_nvenc + hevc_nvenc +
 *                        h264_amf  + hevc_amf  +
 *                        h264_qsv  + hevc_qsv  +
 *                        prores_ks_vulkan +
 *                        mux +
 *                        m4v                        */
#define WINDOWS_MAX_CODECS 12

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

    /* Use UTF-8 for console I/O so drag-and-drop paths with Unicode
     * characters (Cyrillic, CJK, special symbols) are read correctly. */
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

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

    /* Vulkan — GPU-accelerated ProRes (any vendor with Vulkan 1.1+) */
    if (h->support.has_prores_ks_vulkan) {
        h->entries[h->codec_count].name          = "prores_ks_vulkan";
        h->entries[h->codec_count].needs_profile = 1;
        h->entries[h->codec_count].needs_deblock = 0;
        h->codec_count++;
    }

    /* Mux — available only when mkvmerge is found on PATH or next to exe */
    if (platform_mux_is_supported()) {
        h->entries[h->codec_count].name          = "mux";
        h->entries[h->codec_count].needs_profile = 0;
        h->entries[h->codec_count].needs_deblock = 0;
        h->codec_count++;
    }

    /* Apple M4V — available when MP4Box is found on PATH or next to exe */
    if (platform_m4v_is_supported()) {
        h->entries[h->codec_count].name          = "m4v";
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

    /* Mux availability is runtime-probed (mkvmerge on PATH or next to exe) */
    if (!strcmp(codec, "mux"))
        return platform_mux_is_supported();

    if (!h)
        return 0;

    if (!strcmp(codec, "h264_nvenc"))       return h->support.has_h264_nvenc;
    if (!strcmp(codec, "hevc_nvenc"))       return h->support.has_hevc_nvenc;
    if (!strcmp(codec, "h264_amf"))         return h->support.has_h264_amf;
    if (!strcmp(codec, "hevc_amf"))         return h->support.has_hevc_amf;
    if (!strcmp(codec, "h264_qsv"))         return h->support.has_h264_qsv;
    if (!strcmp(codec, "hevc_qsv"))         return h->support.has_hevc_qsv;
    if (!strcmp(codec, "prores_ks_vulkan")) return h->support.has_prores_ks_vulkan;
    if (!strcmp(codec, "m4v"))              return platform_m4v_is_supported();

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
    const char* bin = windows_get_preferred_mkvmerge_bin();
    /* The resolver falls back to the bare name "mkvmerge" when nothing is
     * found; a bare name has no path separator, so treat that as "not found". */
    return bin && (strchr(bin, '\\') != NULL || strchr(bin, '/') != NULL);
}

int platform_m4v_is_supported(void) {
    const char* bin = platform_get_mp4box_bin();
    /* An empty string means the resolver found nothing. */
    return bin && bin[0] != '\0';
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
    /* prores_ks_vulkan: device index used via platform_get_preinput_hw_flags() */
    (void)opts;
    (void)h;
}

int platform_get_default_vulkan_device(const CliPlatformHandle* h) {
    if (h && h->support.has_prores_ks_vulkan)
        return h->support.vulkan_device_index;
    return 1;  /* safe fallback */
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
 *  Mux post-processing
 * --------------------------------------------------------------- */

ConverterError platform_run_mux_postprocess(const ConvertOptions* opts,
                                            const ConverterCallbacks* cb,
                                            const char* input_file)
{
    ConvertOptions file_opts;
    MuxOptions     mux_opts;
    char           effective_output_dir[4096];

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
                 "%s\\ffmpeg_converter", home);
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

/* ---------------------------------------------------------------
 *  Unicode argv
 * --------------------------------------------------------------- */

char** platform_utf8_argv(int argc, char** argv, int* needs_free)
{
    int      i, wargc;
    wchar_t** wargv;
    char**   result;

    *needs_free = 0;

    /* GetCommandLineW/CommandLineToArgvW give us the true Unicode
     * command-line, bypassing the ANSI code-page conversion that the
     * C runtime applies to argv[] when using a plain main() entry point. */
    wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv)
        return argv;

    /* If the argument count doesn't match, something unusual happened. */
    if (wargc != argc) {
        LocalFree(wargv);
        return argv;
    }

    result = (char**)malloc(sizeof(char*) * (size_t)(argc + 1));
    if (!result) {
        LocalFree(wargv);
        return argv;
    }

    for (i = 0; i < argc; i++) {
        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                           NULL, 0, NULL, NULL);
        result[i] = (char*)malloc((size_t)utf8_len);
        if (!result[i]) {
            int j;
            for (j = 0; j < i; j++) free(result[j]);
            free(result);
            LocalFree(wargv);
            return argv;
        }
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                            result[i], utf8_len, NULL, NULL);
    }
    result[argc] = NULL;

    LocalFree(wargv);
    *needs_free = 1;
    return result;
}
