/* platform/converter_windows.c
 * Windows-specific implementations of the converter platform abstraction.
 * Uses Win32 API for binary resolution, path operations, and GPU detection.
 * Targets the MSVC toolchain.
 */

#include "../converter_platform.h"
#include "../converter.h"
#include "runtime_probe.h"  /* WindowsCodecSupport, windows_probe_codec_support */
#include <windows.h>
#include <shlwapi.h>    /* PathFindOnPathA, PathFileExistsA */
#include <direct.h>     /* _mkdir */
#include <io.h>         /* _access */
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

static const char* windows_get_exe_dir(void) {
    static char exe_dir[MAX_PATH] = {0};
    static int initialized = 0;
    if (initialized) return exe_dir;
    initialized = 1;

    char exe_path[MAX_PATH];
    if (GetModuleFileNameA(NULL, exe_path, MAX_PATH) == 0)
        return exe_dir;

    /* Strip the executable filename to get the directory */
    char* last_sep = strrchr(exe_path, '\\');
    if (!last_sep) last_sep = strrchr(exe_path, '/');
    if (last_sep) {
        size_t dir_len = (size_t)(last_sep - exe_path);
        if (dir_len < MAX_PATH) {
            strncpy(exe_dir, exe_path, dir_len);
            exe_dir[dir_len] = '\0';
        }
    }
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

static const char* windows_resolve_bin(const char* env_name,
                                        const char* env_name2,
                                        const char* bin_filename,
                                        char* cache,
                                        int* initialized) {
    if (*initialized) return cache;
    *initialized = 1;

    /* 1. Environment variable */
    if (env_name && env_name[0] != '\0') {
        const char* env = getenv(env_name);
        if (env && env[0] != '\0') {
            strncpy(cache, env, MAX_PATH - 1);
            cache[MAX_PATH - 1] = '\0';
            windows_diag_log("resolved %s via env %s: %s",
                             bin_filename, env_name, cache);
            return cache;
        }
    }
    if (env_name2 && env_name2[0] != '\0') {
        const char* env = getenv(env_name2);
        if (env && env[0] != '\0') {
            strncpy(cache, env, MAX_PATH - 1);
            cache[MAX_PATH - 1] = '\0';
            windows_diag_log("resolved %s via env %s: %s",
                             bin_filename, env_name2, cache);
            return cache;
        }
    }

    /* 2. Bundled next to the .exe */
    const char* exe_dir = windows_get_exe_dir();
    if (exe_dir[0] != '\0') {
        char candidate[MAX_PATH];
        snprintf(candidate, sizeof(candidate), "%s\\%s", exe_dir, bin_filename);
        if (PathFileExistsA(candidate)) {
            strncpy(cache, candidate, MAX_PATH - 1);
            cache[MAX_PATH - 1] = '\0';
            windows_diag_log("resolved %s next to exe: %s", bin_filename, cache);
            return cache;
        }
    }

    /* 3. Search PATH */
    char on_path[MAX_PATH];
    strncpy(on_path, bin_filename, MAX_PATH - 1);
    on_path[MAX_PATH - 1] = '\0';
    if (PathFindOnPathA(on_path, NULL)) {
        strncpy(cache, on_path, MAX_PATH - 1);
        cache[MAX_PATH - 1] = '\0';
        windows_diag_log("resolved %s via PATH: %s", bin_filename, cache);
        return cache;
    }

    cache[0] = '\0';
    windows_diag_log("failed to resolve %s", bin_filename);
    return cache;
}

const char* platform_get_ffmpeg_bin(void) {
    static char cache[MAX_PATH] = {0};
    static int  initialized = 0;
    return windows_resolve_bin("FFMPEG", "FFMPEG_BIN", "ffmpeg.exe",
                                cache, &initialized);
}

const char* platform_get_ffprobe_bin(void) {
    static char cache[MAX_PATH] = {0};
    static int  initialized = 0;
    return windows_resolve_bin("FFPROBE", "FFPROBE_BIN", "ffprobe.exe",
                                cache, &initialized);
}

const char* platform_get_mkvmerge_bin(void) {
    static char cache[MAX_PATH] = {0};
    static int  initialized = 0;
    return windows_resolve_bin("MKVMERGE", NULL, "mkvmerge.exe",
                                cache, &initialized);
}

const char* platform_get_mp4box_bin(void) {
    static char cache[MAX_PATH] = {0};
    static int  initialized = 0;
    return windows_resolve_bin("MP4BOX", NULL, "MP4Box.exe",
                                cache, &initialized);
}

/* ---------------------------------------------------------------
 *  Path operations
 * --------------------------------------------------------------- */

char* platform_escape_path_for_command(const char* path) {
    if (!path) return NULL;

    /* On Windows CMD: wrap in double-quotes and escape embedded double-quotes
     * by doubling them.  We also escape backslashes before a trailing quote. */
    size_t in_len = strlen(path);
    /* Worst case: every char is '"' → doubled + outer 2 + NUL */
    char* out = malloc(2 + in_len * 2 + 1);
    if (!out) return NULL;

    char* p = out;
    *p++ = '"';
    for (size_t i = 0; i < in_len; i++) {
        if (path[i] == '"') {
            *p++ = '"';
            *p++ = '"';
        } else {
            *p++ = path[i];
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
    return (_access(path, R_OK) == 0) ? 1 : 0;
}

int platform_is_dir_writable(const char* path) {
    return (_access(path, W_OK) == 0) ? 1 : 0;
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
        const char* profile_name = "hq"; /* default: HQ */
        if (copt) {
            if      (copt->profile == 1) profile_name = "lt";
            else if (copt->profile == 2) profile_name = "standard";
            else if (copt->profile == 3) profile_name = "hq";
            else if (copt->profile == 4) profile_name = "4444";
        }
        snprintf(prores_flags, sizeof(prores_flags),
                 "-c:v prores_ks_vulkan -profile:v %s ", profile_name);
        return prores_flags;
    }

    /* ProRes is a software codec on Windows — no hwaccel flags needed.
     * The -hwaccel option is an INPUT option (must precede -i); it cannot
     * be included here because these flags are appended after the input. */
    if (strcmp(codec, "prores") == 0) {
        int profile = 2;
        if (copt && copt->profile >= 1 && copt->profile <= 4)
            profile = copt->profile;
        snprintf(prores_flags, sizeof(prores_flags),
                 "-c:v prores -profile:v %d ", profile);
        return prores_flags;
    }

    if (strcmp(codec, "prores_ks") == 0) {
        const char* profile_name = "standard";
        if (copt) {
            if (copt->profile == 1) profile_name = "lt";
            else if (copt->profile == 3) profile_name = "hq";
            else if (copt->profile == 4) profile_name = "4444";
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
    (void)opts;
    if (!codec) return NULL;

    /* Intel QSV: enable hardware-accelerated decode for better
     * performance and to keep frames in GPU memory. */
    if (strcmp(codec, "h264_qsv") == 0 || strcmp(codec, "hevc_qsv") == 0)
        return "-hwaccel qsv";

    /* prores_ks_vulkan requires a Vulkan device context before -i.
     * Use whichever device index passed the startup probe. */
    if (strcmp(codec, "prores_ks_vulkan") == 0) {
        static char vk_flag[48];
        WindowsCodecSupport sup;
        windows_probe_codec_support(&sup);
        snprintf(vk_flag, sizeof(vk_flag),
                 "-init_hw_device vulkan=vk:%d", sup.vulkan_device_index);
        return vk_flag;
    }
    return NULL;
}

const char* platform_get_hw_vfilter(const char* codec, const void* opts)
{
    if (codec && strcmp(codec, "prores_ks_vulkan") == 0) {
        const ConvertOptions* copt = (const ConvertOptions*)opts;
        /* ProRes 4444 uses yuv444p10le; all other profiles use yuv422p10le */
        if (copt && copt->profile == 4)
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
    DWORD attrs;
    if (!path || path[0] == '\0') return 0;
    attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
}

int platform_stat_is_directory(const char *path)
{
    DWORD attrs;
    if (!path || path[0] == '\0') return 0;
    attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
}

FILE *platform_popen(const char *cmd, const char *mode)
{
    FILE* fp;
    char* wrapped;
    size_t cmd_len;

    windows_diag_log("popen mode=%s cmd=%s", mode ? mode : "(null)", cmd ? cmd : "(null)");

    if (!cmd || !mode)
        return NULL;

    /* cmd.exe requires the entire command line to be wrapped in an outer
     * quoted group when the first token is itself a quoted path.
     * Allocate dynamically so we never silently truncate a long command. */
    cmd_len = strlen(cmd);
    wrapped = (char*)malloc(cmd_len + 3); /* '"' + cmd + '"' + '\0' */
    if (!wrapped) {
        windows_diag_log("popen: malloc failed for wrapped command");
        return NULL;
    }
    wrapped[0] = '"';
    memcpy(wrapped + 1, cmd, cmd_len);
    wrapped[cmd_len + 1] = '"';
    wrapped[cmd_len + 2] = '\0';

    fp = _popen(wrapped, mode);
    if (!fp) {
        windows_diag_log("popen wrapped command failed, retrying raw command");
        fp = _popen(cmd, mode);
    }
    free(wrapped);
    return fp;
}

int platform_pclose(FILE *fp)
{
    int rc = _pclose(fp);
    windows_diag_log("pclose rc=%d", rc);
    return rc;
}
