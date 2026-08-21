/* platform/converter_windows.c
 * Windows-specific implementations of the converter platform abstraction.
 * Uses Win32 API for binary resolution, path operations, and GPU detection.
 * Targets the MSVC toolchain.
 */

#include "../converter_platform.h"
#include "../converter.h"
#include <windows.h>
#include <shlwapi.h>    /* PathFindOnPathW */
#include <direct.h>     /* _mkdir */
#include <io.h>         /* _access, _waccess, _wunlink */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <malloc.h>

/* _access mode constants for Windows */
#ifndef R_OK
#  define R_OK 4
#endif
#ifndef W_OK
#  define W_OK 2
#endif

/* ---------------------------------------------------------------
 *  Internal helpers
 * --------------------------------------------------------------- */

static int windows_diag_enabled(void) {
    const char* v = getenv("FFMPEG_CONVERTER_DIAG");
    if (!v || v[0] == '\0')
        v = getenv("FFMPEG_CONVERTER_DEBUG");
    return (v && v[0] != '\0' && strcmp(v, "0") != 0) ? 1 : 0;
}

static void windows_diag_log(const char* fmt, ...) {
    if (!windows_diag_enabled() || !fmt)
        return;

    va_list ap;
    fprintf(stderr, "[windows-diag] ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* Convert a UTF-8 string to a newly allocated wide string.
 * Returns 1 on success (caller must free *out), 0 on failure. */
static int utf8_to_wide(const char* s, wchar_t** out) {
    int wlen;
    if (!s || !out) return 0;
    wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (wlen <= 0) return 0;
    *out = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
    if (!*out) return 0;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, *out, wlen);
    return 1;
}

/* Check whether a UTF-8 path refers to an existing filesystem entry. */
static int win_file_exists_utf8(const char* path) {
    wchar_t* wpath = NULL;
    DWORD attrs;
    if (!path || path[0] == '\0') return 0;
    if (!utf8_to_wide(path, &wpath)) return 0;
    attrs = GetFileAttributesW(wpath);
    free(wpath);
    return attrs != INVALID_FILE_ATTRIBUTES;
}

/* Returns the directory of the running executable as a UTF-8 string.
 * Uses GetModuleFileNameW to support Unicode paths. */
static const char* windows_get_exe_dir(void) {
    static char exe_dir[4096] = {0};
    static int initialized = 0;
    if (initialized) return exe_dir;
    initialized = 1;

    wchar_t exe_path[4096];
    if (GetModuleFileNameW(NULL, exe_path, 4096) == 0)
        return exe_dir;

    /* Strip the executable filename to get the directory */
    wchar_t* last_sep = wcsrchr(exe_path, L'\\');
    wchar_t* slash    = wcsrchr(exe_path, L'/');
    if (slash && (!last_sep || slash > last_sep))
        last_sep = slash;
    if (last_sep)
        *last_sep = L'\0';

    WideCharToMultiByte(CP_UTF8, 0, exe_path, -1,
                        exe_dir, (int)(sizeof(exe_dir) - 1), NULL, NULL);
    exe_dir[sizeof(exe_dir) - 1] = '\0';
    return exe_dir;
}

/* ---------------------------------------------------------------
 *  Lifecycle
 * --------------------------------------------------------------- */

int platform_init(void) {
    /* No heavy initialisation needed on Windows.
     * Binary resolution is done lazily on first call. */
    return 0;
}

void platform_cleanup(void) {
    /* Nothing to release on Windows. */
}

/* ---------------------------------------------------------------
 *  Binary resolution
 * --------------------------------------------------------------- */

/*
 * windows_resolve_ffbin — resolve ffmpeg or ffprobe.
 *
 * Search order: environment variable → binary bundled next to the .exe.
 * PATH is intentionally NOT searched: only the version shipped with the
 * program is accepted, matching the behaviour of the macOS implementation.
 */
static const char* windows_resolve_ffbin(const char* env_name,
                                          const char* env_name2,
                                          const char* bin_filename,
                                          char* cache,
                                          size_t cache_sz,
                                          int* initialized) {
    if (*initialized) return cache;
    *initialized = 1;

    /* 1. Environment variable */
    if (env_name && env_name[0] != '\0') {
        const char* env = getenv(env_name);
        if (env && env[0] != '\0') {
            strncpy(cache, env, cache_sz - 1);
            cache[cache_sz - 1] = '\0';
            windows_diag_log("resolved %s via env %s: %s",
                             bin_filename, env_name, cache);
            return cache;
        }
    }
    if (env_name2 && env_name2[0] != '\0') {
        const char* env = getenv(env_name2);
        if (env && env[0] != '\0') {
            strncpy(cache, env, cache_sz - 1);
            cache[cache_sz - 1] = '\0';
            windows_diag_log("resolved %s via env %s: %s",
                             bin_filename, env_name2, cache);
            return cache;
        }
    }

    /* 2. Bundled next to the .exe (Unicode-safe) */
    const char* exe_dir = windows_get_exe_dir();
    if (exe_dir[0] != '\0') {
        char candidate[4096];
        snprintf(candidate, sizeof(candidate), "%s\\%s", exe_dir, bin_filename);
        if (win_file_exists_utf8(candidate)) {
            strncpy(cache, candidate, cache_sz - 1);
            cache[cache_sz - 1] = '\0';
            windows_diag_log("resolved %s next to exe: %s", bin_filename, cache);
            return cache;
        }
    }

    cache[0] = '\0';
    windows_diag_log("failed to resolve %s (not bundled)", bin_filename);
    return cache;
}

/*
 * windows_resolve_tool — resolve an optional tool (mkvmerge, MP4Box).
 *
 * Search order: environment variable → bundled next to the .exe → PATH.
 * These tools are not required to be bundled; system installations are
 * acceptable.
 */
static const char* windows_resolve_tool(const char* env_name,
                                         const char* bin_filename,
                                         char* cache,
                                         size_t cache_sz,
                                         int* initialized) {
    if (*initialized) return cache;
    *initialized = 1;

    /* 1. Environment variable */
    if (env_name && env_name[0] != '\0') {
        const char* env = getenv(env_name);
        if (env && env[0] != '\0') {
            strncpy(cache, env, cache_sz - 1);
            cache[cache_sz - 1] = '\0';
            windows_diag_log("resolved %s via env %s: %s",
                             bin_filename, env_name, cache);
            return cache;
        }
    }

    /* 2. Bundled next to the .exe */
    const char* exe_dir = windows_get_exe_dir();
    if (exe_dir[0] != '\0') {
        char candidate[4096];
        snprintf(candidate, sizeof(candidate), "%s\\%s", exe_dir, bin_filename);
        if (win_file_exists_utf8(candidate)) {
            strncpy(cache, candidate, cache_sz - 1);
            cache[cache_sz - 1] = '\0';
            windows_diag_log("resolved %s next to exe: %s", bin_filename, cache);
            return cache;
        }
    }

    /* 3. Search PATH (Unicode-safe via PathFindOnPathW) */
    {
        wchar_t* wbin = NULL;
        if (utf8_to_wide(bin_filename, &wbin)) {
            wchar_t on_path[MAX_PATH];
            wcsncpy(on_path, wbin, MAX_PATH - 1);
            on_path[MAX_PATH - 1] = L'\0';
            free(wbin);
            if (PathFindOnPathW(on_path, NULL)) {
                WideCharToMultiByte(CP_UTF8, 0, on_path, -1,
                                    cache, (int)(cache_sz - 1), NULL, NULL);
                cache[cache_sz - 1] = '\0';
                windows_diag_log("resolved %s via PATH: %s", bin_filename, cache);
                return cache;
            }
        }
    }

    cache[0] = '\0';
    windows_diag_log("failed to resolve %s", bin_filename);
    return cache;
}

const char* platform_get_ffmpeg_bin(void) {
    static char cache[4096] = {0};
    static int  initialized = 0;
    return windows_resolve_ffbin("FFMPEG", "FFMPEG_BIN", "ffmpeg.exe",
                                  cache, sizeof(cache), &initialized);
}

const char* platform_get_ffprobe_bin(void) {
    static char cache[4096] = {0};
    static int  initialized = 0;
    return windows_resolve_ffbin("FFPROBE", "FFPROBE_BIN", "ffprobe.exe",
                                  cache, sizeof(cache), &initialized);
}

const char* platform_get_mkvmerge_bin(void) {
    static char cache[4096] = {0};
    static int  initialized = 0;
    return windows_resolve_tool("MKVMERGE", "mkvmerge.exe",
                                 cache, sizeof(cache), &initialized);
}

const char* platform_get_mp4box_bin(void) {
    static char cache[4096] = {0};
    static int  initialized = 0;
    return windows_resolve_tool("MP4BOX", "MP4Box.exe",
                                 cache, sizeof(cache), &initialized);
}

/* ---------------------------------------------------------------
 *  Path operations
 * --------------------------------------------------------------- */

char* platform_escape_path_for_command(const char* path) {
    if (!path) return NULL;

    /* Windows cmd.exe / CommandLineToArgvW quoting rules:
     *  - Wrap the argument in double-quotes.
     *  - Backslashes are literal unless immediately before a double-quote.
     *  - Backslashes before a double-quote: emit 2*N backslashes then \".
     *  - Backslashes at end of string (before closing "): emit 2*N backslashes.
     *  - A literal double-quote inside: escaped as \".
     * Worst-case output: 2 * in_len + 3 bytes (all-backslash + closing quote). */
    size_t in_len = strlen(path);
    char* out = malloc(2 + in_len * 2 + 1);
    if (!out) return NULL;

    char* p = out;
    *p++ = '"';

    const char* s = path;
    while (*s != '\0') {
        /* Count consecutive backslashes */
        size_t n_bs = 0;
        while (s[n_bs] == '\\') ++n_bs;

        if (s[n_bs] == '"') {
            /* Backslashes immediately before a double-quote:
             * emit 2*N backslashes then \" */
            for (size_t i = 0; i < n_bs * 2; i++) *p++ = '\\';
            *p++ = '\\';
            *p++ = '"';
            s += n_bs + 1;
        } else if (s[n_bs] == '\0') {
            /* Backslashes at end of string (before closing "): emit 2*N */
            for (size_t i = 0; i < n_bs * 2; i++) *p++ = '\\';
            s += n_bs;
        } else {
            /* Backslashes not before a quote: emit as-is, then the char */
            for (size_t i = 0; i < n_bs; i++) *p++ = '\\';
            *p++ = s[n_bs];
            s += n_bs + 1;
        }
    }

    *p++ = '"';
    *p   = '\0';
    return out;
}

int platform_mkdir_recursive(const char* path) {
    if (!path || path[0] == '\0')
        return -1;

    char tmp[MAX_PATH];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    strcpy(tmp, path);

    /* Strip trailing separator */
    if (len > 1 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\'))
        tmp[len - 1] = '\0';

    /* Walk path and create each component */
    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char sep = *p;
            *p = '\0';
            /* Skip drive letter root: "C:" alone is invalid for _mkdir */
            if (!(p == tmp + 2 && tmp[1] == ':'))
                if (_mkdir(tmp) != 0 && errno != EEXIST)
                    return -1;
            *p = sep;
        }
    }

    if (_mkdir(tmp) != 0 && errno != EEXIST)
        return -1;

    return 0;
}

const char* platform_get_home_dir(void) {
    static char home_dir[MAX_PATH] = {0};
    static int  initialized = 0;
    if (initialized) return home_dir;
    initialized = 1;

    const char* v = getenv("USERPROFILE");
    if (v && v[0] != '\0') {
        strncpy(home_dir, v, MAX_PATH - 1);
        home_dir[MAX_PATH - 1] = '\0';
        return home_dir;
    }

    /* Fallback: HOMEDRIVE + HOMEPATH */
    const char* drive = getenv("HOMEDRIVE");
    const char* hpath = getenv("HOMEPATH");
    if (drive && hpath && drive[0] != '\0' && hpath[0] != '\0') {
        snprintf(home_dir, sizeof(home_dir), "%s%s", drive, hpath);
        return home_dir;
    }

    /* Last resort */
    strncpy(home_dir, "C:\\", MAX_PATH - 1);
    home_dir[MAX_PATH - 1] = '\0';
    return home_dir;
}

const char* platform_get_filename(const char* path) {
    if (!path) return path;
    const char* last_sep = strrchr(path, '\\');
    const char* slash    = strrchr(path, '/');
    if (slash && (!last_sep || slash > last_sep))
        last_sep = slash;
    return last_sep ? last_sep + 1 : path;
}

char* platform_join_paths(const char* dir, const char* file) {
    if (!dir || !file) return NULL;
    size_t dir_len  = strlen(dir);
    /* Strip trailing path separators from dir */
    while (dir_len > 0 && (dir[dir_len - 1] == '\\' || dir[dir_len - 1] == '/'))
        --dir_len;
    size_t file_len = strlen(file);
    /* dir + "\" + file + NUL */
    char* out = malloc(dir_len + 1 + file_len + 1);
    if (!out) return NULL;
    memcpy(out, dir, dir_len);
    out[dir_len] = '\\';
    memcpy(out + dir_len + 1, file, file_len + 1);
    return out;
}

int platform_path_is_absolute(const char* path) {
    if (!path || path[0] == '\0') return 0;
    /* Drive-letter absolute: "C:\..." or "C:/..." */
    if (path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
        return 1;
    /* UNC absolute: "\\server\..." */
    if (path[0] == '\\' && path[1] == '\\')
        return 1;
    /* POSIX-style absolute path */
    if (path[0] == '/')
        return 1;
    return 0;
}

const char* platform_get_null_device(void) {
    return "nul";
}

int platform_is_file_readable(const char* path) {
    wchar_t* wpath = NULL;
    int result;
    if (!path || path[0] == '\0') return 0;
    if (!utf8_to_wide(path, &wpath)) return 0;
    result = (_waccess(wpath, R_OK) == 0) ? 1 : 0;
    free(wpath);
    return result;
}

int platform_is_dir_writable(const char* path) {
    wchar_t* wpath = NULL;
    int result;
    if (!path || path[0] == '\0') return 0;
    if (!utf8_to_wide(path, &wpath)) return 0;
    result = (_waccess(wpath, W_OK) == 0) ? 1 : 0;
    free(wpath);
    return result;
}

/* ---------------------------------------------------------------
 *  Output handling
 * --------------------------------------------------------------- */

void platform_normalize_output_line(char* line) {
    if (!line) return;
    size_t len = strlen(line);
    /* Strip trailing \r before \n (Windows CRLF line endings) */
    if (len >= 2 && line[len - 2] == '\r' && line[len - 1] == '\n') {
        line[len - 2] = '\n';
        line[len - 1] = '\0';
    }
}

/* ---------------------------------------------------------------
 *  Audio and GPU support
 * --------------------------------------------------------------- */

int platform_validate_audio_filters(void) {
    /* Check that the ffmpeg binary actually contains the libsoxr resampler.
     * A minimal ffmpeg build without libsoxr will fail at the aresample filter. */
    const char* ffmpeg = platform_get_ffmpeg_bin();
    if (!ffmpeg || ffmpeg[0] == '\0')
        return 0;

    char cmd[MAX_PATH + 64];
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -hide_banner -v error -filters 2>nul", ffmpeg);

    FILE* fp = _popen(cmd, "r");
    if (!fp)
        return 0;

    char line[512];
    int found_soxr = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "aresample") || strstr(line, "soxr")) {
            found_soxr = 1;
            break;
        }
    }
    _pclose(fp);
    return found_soxr;
}

int platform_supports_codec(const char* codec) {
    if (!codec) return 0;

    /* Cross-platform codecs */
    if (strcmp(codec, "copy")      == 0 ||
        strcmp(codec, "prores")    == 0 ||
        strcmp(codec, "prores_ks") == 0)
        return 1;

    /* Windows hardware codecs — check availability at runtime */
    if (strcmp(codec, "h264_nvenc")       == 0 ||
        strcmp(codec, "hevc_nvenc")       == 0 ||
        strcmp(codec, "h264_amf")         == 0 ||
        strcmp(codec, "hevc_amf")         == 0 ||
        strcmp(codec, "h264_qsv")         == 0 ||
        strcmp(codec, "hevc_qsv")         == 0 ||
        strcmp(codec, "prores_ks_vulkan") == 0)
    {
        int caps = platform_detect_gpu_support();
        if (strcmp(codec, "h264_nvenc")       == 0) return (caps & PLAT_CAP_NVENC_H264)     ? 1 : 0;
        if (strcmp(codec, "hevc_nvenc")       == 0) return (caps & PLAT_CAP_NVENC_HEVC)     ? 1 : 0;
        if (strcmp(codec, "h264_amf")         == 0) return (caps & PLAT_CAP_AMF_H264)       ? 1 : 0;
        if (strcmp(codec, "hevc_amf")         == 0) return (caps & PLAT_CAP_AMF_HEVC)       ? 1 : 0;
        if (strcmp(codec, "h264_qsv")         == 0) return (caps & PLAT_CAP_QSV_H264)       ? 1 : 0;
        if (strcmp(codec, "hevc_qsv")         == 0) return (caps & PLAT_CAP_QSV_HEVC)       ? 1 : 0;
        if (strcmp(codec, "prores_ks_vulkan") == 0) return (caps & PLAT_CAP_VULKAN_PRORES)  ? 1 : 0;
    }

    /* Linux / macOS platform-specific codecs are not supported on Windows */
    return 0;
}

const char* platform_get_video_codec_flags(const char* codec,
                                            const char* input_path,
                                            const void* opts) {
    (void)input_path;

    const ConvertOptions* copt = (const ConvertOptions*)opts;
    static char prores_flags[256];

    if (!codec) return NULL;

    if (strcmp(codec, "h264_nvenc") == 0)
        return "-c:v h264_nvenc -preset p7 -qp 22 -spatial_aq 1 -temporal_aq 1 ";
    if (strcmp(codec, "hevc_nvenc") == 0)
        return "-c:v hevc_nvenc -preset hq -cq 25 -lookahead_level auto ";
    if (strcmp(codec, "h264_amf") == 0)
        return "-c:v h264_amf ";
    if (strcmp(codec, "hevc_amf") == 0)
        return "-c:v hevc_amf ";
    if (strcmp(codec, "h264_qsv") == 0)
        return "-c:v h264_qsv -global_quality 22 -preset slower "
               "-look_ahead 1 -look_ahead_depth 40 -extbrc 1 ";
    if (strcmp(codec, "hevc_qsv") == 0)
        return "-c:v hevc_qsv -global_quality 25 -preset slow "
               "-g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1 ";

    if (strcmp(codec, "prores_ks_vulkan") == 0) {
        const char* profile_name = "hq"; /* default: HQ (preserves pre-refactor behavior) */
        if (copt && copt->preset[0] != '\0') {
            if      (strcmp((const char*)copt->preset, "lt") == 0)     profile_name = "lt";
            else if (strcmp((const char*)copt->preset, "standard") == 0) profile_name = "standard";
            else if (strcmp((const char*)copt->preset, "hq") == 0)     profile_name = "hq";
            else if (strcmp((const char*)copt->preset, "4444") == 0)   profile_name = "4444";
            else profile_name = "hq";  /* unknown, use HQ */
        }
        snprintf(prores_flags, sizeof(prores_flags),
                 "-c:v prores_ks_vulkan -profile:v %s ", profile_name);
        return prores_flags;
    }

    /* ProRes is a software codec on Windows — no hwaccel flags needed.
     * The -hwaccel option is an INPUT option (must precede -i); it cannot
     * be included here because these flags are appended after the input. */
    if (strcmp(codec, "prores") == 0) {
        int profile = 2;  /* standard */
        if (copt && copt->preset[0] != '\0') {
            if (strcmp((const char*)copt->preset, "lt") == 0) profile = 1;
            else if (strcmp((const char*)copt->preset, "hq") == 0) profile = 3;
            else if (strcmp((const char*)copt->preset, "4444") == 0) profile = 4;
            else profile = 2;
        }
        snprintf(prores_flags, sizeof(prores_flags),
                 "-c:v prores -profile:v %d ", profile);
        return prores_flags;
    }

    if (strcmp(codec, "prores_ks") == 0) {
        const char* profile_name = "standard";
        if (copt && copt->preset[0] != '\0') {
            if (strcmp((const char*)copt->preset, "lt") == 0) profile_name = "lt";
            else if (strcmp((const char*)copt->preset, "hq") == 0) profile_name = "hq";
            else if (strcmp((const char*)copt->preset, "4444") == 0) profile_name = "4444";
            else profile_name = "standard";
        }
        snprintf(prores_flags, sizeof(prores_flags),
                 "-c:v prores_ks -profile:v %s ", profile_name);
        return prores_flags;
    }

    /* Not a Windows platform-specific codec */
    return NULL;
}

int platform_detect_gpu_support(void) {
    static int caps = -1;
    if (caps != -1) return caps;
    caps = 0;

    const char* ffmpeg = platform_get_ffmpeg_bin();
    if (!ffmpeg || ffmpeg[0] == '\0') return caps;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -hide_banner -v error -encoders 2>nul", ffmpeg);

    FILE* fp = _popen(cmd, "r");
    if (!fp) return caps;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        platform_normalize_output_line(line);
        if (strstr(line, " h264_nvenc")) caps |= PLAT_CAP_NVENC_H264;
        if (strstr(line, " hevc_nvenc")) caps |= PLAT_CAP_NVENC_HEVC;
        if (strstr(line, " h264_amf"))   caps |= PLAT_CAP_AMF_H264;
        if (strstr(line, " hevc_amf"))   caps |= PLAT_CAP_AMF_HEVC;
        if (strstr(line, " h264_qsv"))          caps |= PLAT_CAP_QSV_H264;
        if (strstr(line, " hevc_qsv"))          caps |= PLAT_CAP_QSV_HEVC;
        if (strstr(line, " prores_ks_vulkan"))  caps |= PLAT_CAP_VULKAN_PRORES;
        if (strstr(line, " libfdk_aac"))        caps |= PLAT_CAP_LIBFDK_AAC;
    }
    _pclose(fp);

    /* Probe decoders: flag av1_qsv only when Intel QSV is present.
     * av1_qsv uses D3D11VA via Intel Arc/UHD and reliably decodes AV1
     * without triggering the NVDEC pixel-format negotiation bug in the
     * native av1 decoder (which fails on GPUs without AV1 NVDEC support). */
    if (caps & (PLAT_CAP_QSV_H264 | PLAT_CAP_QSV_HEVC)) {
        snprintf(cmd, sizeof(cmd),
                 "\"%s\" -hide_banner -v error -decoders 2>nul", ffmpeg);
        fp = _popen(cmd, "r");
        if (fp) {
            while (fgets(line, sizeof(line), fp)) {
                platform_normalize_output_line(line);
                if (strstr(line, " av1_qsv"))
                    caps |= PLAT_CAP_AV1_QSV_DEC;
                if (strstr(line, " libdav1d"))
                    caps |= PLAT_CAP_LIBDAV1D_DEC;
            }
            _pclose(fp);
        }
    } else {
        /* No QSV — still probe for libdav1d (software AV1 decode fallback) */
        snprintf(cmd, sizeof(cmd),
                 "\"%s\" -hide_banner -v error -decoders 2>nul", ffmpeg);
        fp = _popen(cmd, "r");
        if (fp) {
            while (fgets(line, sizeof(line), fp)) {
                platform_normalize_output_line(line);
                if (strstr(line, " libdav1d"))
                    caps |= PLAT_CAP_LIBDAV1D_DEC;
            }
            _pclose(fp);
        }
    }

    return caps;
}

int platform_get_hw_device_for_codec(const char* codec,
                                     char* hw_device,
                                     size_t hw_device_sz) {
    (void)codec;
    (void)hw_device;
    (void)hw_device_sz;
    /* Windows GPU codecs (NVENC, QSV) do not require an explicit device path.
     * The encoder selects the device automatically. */
    return 0;
}

/* ---------------------------------------------------------------
 *  Utilities
 * --------------------------------------------------------------- */

int platform_get_cpu_count(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int n = (int)si.dwNumberOfProcessors;
    if (n < 1) n = 1;
    return n;
}

int platform_get_video_info(const char* input_path,
                             int* width, int* height, double* fps) {
    /* Not needed for current Windows codecs (NVENC/QSV do not require
     * an explicit bitrate calculation at this time). */
    (void)input_path;
    if (width)  *width  = 0;
    if (height) *height = 0;
    if (fps)    *fps    = 0.0;
    return 0;
}

/* ---------------------------------------------------------------
 *  Vulkan hardware pipeline hooks
 * --------------------------------------------------------------- */

const char* platform_get_preinput_hw_flags(const char* codec,
                                            const void* opts)
{
    if (!codec) return NULL;

    /* prores_ks_vulkan requires a Vulkan device context before -i.
     * Use whichever device index passed the startup probe. */
    if (strcmp(codec, "prores_ks_vulkan") == 0) {
        static char vk_flag[64];
        const ConvertOptions* copt = (const ConvertOptions*)opts;
        int vk_idx = (copt && copt->vulkan_device >= 0) ? copt->vulkan_device : 1;
        snprintf(vk_flag, sizeof(vk_flag),
                 "-init_hw_device vulkan=vk:%d -filter_hw_device vk", vk_idx);
        return vk_flag;
    }
    (void)opts;
    return NULL;
}

const char* platform_get_hw_vfilter(const char* codec, const void* opts)
{
    if (codec && strcmp(codec, "prores_ks_vulkan") == 0) {
        const ConvertOptions* copt = (const ConvertOptions*)opts;
        /* ProRes 4444 uses yuv444p10le; all other profiles use yuv422p10le */
        if (copt && copt->preset[0] != '\0' && strcmp((const char*)copt->preset, "4444") == 0)
            return "yuv444p10le,hwupload";
        return "yuv422p10le,hwupload";
    }
    return NULL;
}

/* ---------------------------------------------------------------
 *  File-system and process helpers
 * --------------------------------------------------------------- */

int platform_stat_is_regular_file(const char *path)
{
    wchar_t* wpath = NULL;
    DWORD attrs;
    if (!path || path[0] == '\0') return 0;
    if (!utf8_to_wide(path, &wpath)) return 0;
    attrs = GetFileAttributesW(wpath);
    free(wpath);
    return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
}

int platform_stat_is_directory(const char *path)
{
    wchar_t* wpath = NULL;
    DWORD attrs;
    if (!path || path[0] == '\0') return 0;
    if (!utf8_to_wide(path, &wpath)) return 0;
    attrs = GetFileAttributesW(wpath);
    free(wpath);
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
}

FILE *platform_popen(const char *cmd, const char *mode)
{
    FILE*    fp;
    wchar_t* wcmd  = NULL;
    wchar_t* wmode = NULL;
    int      wlen;

    windows_diag_log("popen mode=%s cmd=%s", mode ? mode : "(null)", cmd ? cmd : "(null)");

    if (!cmd || !mode)
        return NULL;

    /* Convert the UTF-8 command and mode strings to wide strings so that
     * _wpopen passes them to cmd.exe as Unicode.  This allows file paths
     * that contain characters outside the current ANSI code page (e.g.
     * Cyrillic, CJK, special symbols) to be handled correctly. */
    wlen = MultiByteToWideChar(CP_UTF8, 0, cmd, -1, NULL, 0);
    if (wlen <= 0) goto fallback;
    wcmd = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wcmd) goto fallback;
    MultiByteToWideChar(CP_UTF8, 0, cmd, -1, wcmd, wlen);

    wlen = MultiByteToWideChar(CP_UTF8, 0, mode, -1, NULL, 0);
    if (wlen <= 0) goto fallback;
    wmode = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wmode) goto fallback;
    MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, wlen);

    /* cmd.exe requires the entire command line to be wrapped in an outer
     * quoted group when the first token is itself a quoted path. */
    if (wcmd[0] == L'"') {
        size_t cmd_wlen = wcslen(wcmd);
        wchar_t* wrapped = (wchar_t*)malloc((cmd_wlen + 3) * sizeof(wchar_t));
        if (!wrapped) goto fallback;
        wrapped[0] = L'"';
        memcpy(wrapped + 1, wcmd, cmd_wlen * sizeof(wchar_t));
        wrapped[cmd_wlen + 1] = L'"';
        wrapped[cmd_wlen + 2] = L'\0';
        fp = _wpopen(wrapped, wmode);
        free(wrapped);
    } else {
        fp = _wpopen(wcmd, wmode);
    }

    free(wcmd);
    free(wmode);

    if (!fp)
        windows_diag_log("_wpopen failed");

    return fp;

fallback:
    free(wcmd);
    free(wmode);
    windows_diag_log("popen: wide conversion failed, falling back to _popen");
    return _popen(cmd, mode);
}

int platform_pclose(FILE *fp)
{
    int rc = _pclose(fp);
    windows_diag_log("pclose rc=%d", rc);
    return rc;
}
