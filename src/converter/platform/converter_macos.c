/* platform/converter_macos.c
 * macOS-specific implementations of the converter platform abstraction.
 * Handles VideoToolbox codecs, MacPorts binary resolution, and
 * macOS-specific system queries.
 */

#include "../converter_platform.h"
#include "../converter.h"   /* ConvertOptions struct for profile field access */
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* ---------------------------------------------------------------
 *  Internal helpers
 * --------------------------------------------------------------- */

static const char* macos_get_exe_dir(void) {
    static char exe_dir[4096] = {0};
    static int initialized = 0;
    if (initialized) return exe_dir;
    initialized = 1;

    char exe_path[4096];
    uint32_t size = (uint32_t)sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) == 0) {
        char resolved[4096];
        if (realpath(exe_path, resolved)) {
            strncpy(exe_dir, dirname(resolved), sizeof(exe_dir) - 1);
            exe_dir[sizeof(exe_dir) - 1] = '\0';
        }
    }
    return exe_dir;
}

static const char* macos_resolve_bundled_bin(const char* name) {
    const char* exe_dir = macos_get_exe_dir();
    if (!exe_dir || exe_dir[0] == '\0') return NULL;

    static char path[4096];

    snprintf(path, sizeof(path), "%s/%s", exe_dir, name);
    if (access(path, X_OK) == 0) return path;

    snprintf(path, sizeof(path), "%s/../Resources/bin/%s", exe_dir, name);
    if (access(path, X_OK) == 0) return path;

    return NULL;
}

/**
 * Probes video stream info (width, height, fps) via ffprobe.
 * Internal helper for bitrate calculation on macOS VideoToolbox.
 */
static void macos_get_video_info(const char* input,
                                  int* out_width,
                                  int* out_height,
                                  double* out_fps) {
    *out_width  = 0;
    *out_height = 0;
    *out_fps    = 0.0;

    char cmd[2048];
    const char* ffprobe_bin = platform_get_ffprobe_bin();
    if (!ffprobe_bin || ffprobe_bin[0] == '\0') return;

    char* esc_ffprobe = platform_escape_path_for_command(ffprobe_bin);
    char* esc_input   = platform_escape_path_for_command(input);
    if (!esc_ffprobe || !esc_input) {
        free(esc_ffprobe);
        free(esc_input);
        return;
    }

    snprintf(cmd, sizeof(cmd),
             "%s -v error -select_streams v:0"
             " -show_entries stream=width,height,r_frame_rate"
             " -of default=noprint_wrappers=1:nokey=1 %s 2>/dev/null",
             esc_ffprobe, esc_input);

    free(esc_ffprobe);
    free(esc_input);

    FILE* fp = popen(cmd, "r");
    if (!fp) return;

    char line[256];
    if (fgets(line, sizeof(line), fp)) *out_width  = atoi(line);
    if (fgets(line, sizeof(line), fp)) *out_height = atoi(line);
    if (fgets(line, sizeof(line), fp)) {
        int num = 0, den = 1;
        if (sscanf(line, "%d/%d", &num, &den) == 2 && den > 0)
            *out_fps = (double)num / (double)den;
        else
            *out_fps = atof(line);
    }
    pclose(fp);
}

/**
 * Sub-linear bits-per-pixel bitrate formula:
 *   base = 35000 kbps @ 4K (3840×2160) / 24 fps
 *   bitrate = base × (pixels / base_pixels) × (fps / base_fps)^0.75
 *   clamped to [2000, 80000] kbps
 */
static int macos_calc_hevc_vt_bitrate_kbps(int width, int height, double fps) {
    if (width <= 0 || height <= 0 || fps <= 0.0) return 35000;

    const double BASE_KBPS   = 35000.0;
    const double BASE_PIXELS = 3840.0 * 2160.0;
    const double BASE_FPS    = 24.0;

    double pixel_ratio = (double)(width * height) / BASE_PIXELS;
    double fps_ratio   = pow(fps / BASE_FPS, 0.75);
    double kbps        = BASE_KBPS * pixel_ratio * fps_ratio;

    if (kbps < 2000.0)  kbps = 2000.0;
    if (kbps > 80000.0) kbps = 80000.0;
    return (int)kbps;
}

/**
 * Same sub-linear bits-per-pixel formula as HEVC, but with a higher base
 * bitrate: H.264 is a less efficient codec than HEVC, so it needs roughly
 * 1.4x the bitrate to reach comparable visual quality.
 *   base = 50000 kbps @ 4K (3840×2160) / 24 fps
 *   clamped to [2500, 100000] kbps
 */
static int macos_calc_h264_vt_bitrate_kbps(int width, int height, double fps) {
    if (width <= 0 || height <= 0 || fps <= 0.0) return 50000;

    const double BASE_KBPS   = 50000.0;
    const double BASE_PIXELS = 3840.0 * 2160.0;
    const double BASE_FPS    = 24.0;

    double pixel_ratio = (double)(width * height) / BASE_PIXELS;
    double fps_ratio   = pow(fps / BASE_FPS, 0.75);
    double kbps        = BASE_KBPS * pixel_ratio * fps_ratio;

    if (kbps < 2500.0)   kbps = 2500.0;
    if (kbps > 100000.0) kbps = 100000.0;
    return (int)kbps;
}

/* ---------------------------------------------------------------
 *  Lifecycle
 * --------------------------------------------------------------- */

int platform_init(void) {
    /* No heavy initialisation needed on macOS.
     * Binary resolution is done lazily. */
    return 0;
}

void platform_cleanup(void) {
    /* Nothing to release on macOS. */
}

/* ---------------------------------------------------------------
 *  Binary resolution
 * --------------------------------------------------------------- */

const char* platform_get_ffmpeg_bin(void) {
    const char* v = getenv("FFMPEG");
    if (v && v[0] != '\0') return v;
    v = getenv("FFMPEG_BIN");
    if (v && v[0] != '\0') return v;

    /* Bundled binary (app bundle or CLI-adjacent) */
    const char* bundled = macos_resolve_bundled_bin("ffmpeg");
    if (bundled) return bundled;

    return "";
}

const char* platform_get_ffprobe_bin(void) {
    const char* v = getenv("FFPROBE");
    if (v && v[0] != '\0') return v;
    v = getenv("FFPROBE_BIN");
    if (v && v[0] != '\0') return v;

    /* Bundled binary (app bundle or CLI-adjacent) */
    const char* bundled = macos_resolve_bundled_bin("ffprobe");
    if (bundled) return bundled;

    return "";
}

const char* platform_get_mkvmerge_bin(void) {
    const char* v = getenv("MKVMERGE");
    if (v && v[0] != '\0') return v;

    /* App bundle */
    const char* bundled = macos_resolve_bundled_bin("mkvmerge");
    if (bundled) return bundled;

    if (access("/opt/local/bin/mkvmerge", X_OK) == 0)
        return "/opt/local/bin/mkvmerge";
    if (access("/usr/local/bin/mkvmerge", X_OK) == 0)
        return "/usr/local/bin/mkvmerge";
    if (access("/opt/homebrew/bin/mkvmerge", X_OK) == 0)
        return "/opt/homebrew/bin/mkvmerge";

    return "";
}

const char* platform_get_mp4box_bin(void) {
    const char* v = getenv("MP4BOX");
    if (v && v[0] != '\0') return v;

    /* App bundle */
    const char* bundled = macos_resolve_bundled_bin("MP4Box");
    if (bundled) return bundled;

    if (access("/opt/local/bin/MP4Box", X_OK) == 0)
        return "/opt/local/bin/MP4Box";
    if (access("/usr/local/bin/MP4Box", X_OK) == 0)
        return "/usr/local/bin/MP4Box";

    return "";
}

/* ---------------------------------------------------------------
 *  Path operations
 * --------------------------------------------------------------- */

char* platform_escape_path_for_command(const char* path) {
    if (!path) return NULL;

    size_t in_len = strlen(path);
    char* out = malloc(2 + in_len * 4 + 1);
    if (!out) return NULL;

    char* p = out;
    *p++ = '\'';
    for (size_t i = 0; i < in_len; i++) {
        if (path[i] == '\'') {
            *p++ = '\'';
            *p++ = '\\';
            *p++ = '\'';
            *p++ = '\'';
        } else {
            *p++ = path[i];
        }
    }
    *p++ = '\'';
    *p   = '\0';
    return out;
}

int platform_mkdir_recursive(const char* path) {
    if (!path || path[0] == '\0')
        return -1;

    char tmp[4096];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    strcpy(tmp, path);

    if (len > 1 && tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;

    return 0;
}

const char* platform_get_home_dir(void) {
    const char* v = getenv("HOME");
    if (v && v[0] != '\0') return v;
    return ".";
}

const char* platform_get_filename(const char* path) {
    if (!path) return path;
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

char* platform_join_paths(const char* dir, const char* file) {
    if (!dir || !file) return NULL;
    size_t dir_len  = strlen(dir);
    size_t file_len = strlen(file);
    char* out = malloc(dir_len + 1 + file_len + 1);
    if (!out) return NULL;
    memcpy(out, dir, dir_len);
    out[dir_len] = '/';
    memcpy(out + dir_len + 1, file, file_len + 1);
    return out;
}

int platform_path_is_absolute(const char* path) {
    return (path && path[0] == '/') ? 1 : 0;
}

const char* platform_get_null_device(void) {
    return "/dev/null";
}

int platform_is_file_readable(const char* path) {
    return (access(path, R_OK) == 0) ? 1 : 0;
}

int platform_is_dir_writable(const char* path) {
    return (access(path, W_OK) == 0) ? 1 : 0;
}

/* ---------------------------------------------------------------
 *  Output handling
 * --------------------------------------------------------------- */

void platform_normalize_output_line(char* line) {
    (void)line;  /* no-op: macOS ffmpeg outputs \n only */
}

/* ---------------------------------------------------------------
 *  Audio and GPU support
 * --------------------------------------------------------------- */

int platform_validate_audio_filters(void) {
    const char* ffmpeg = platform_get_ffmpeg_bin();
    if (!ffmpeg || ffmpeg[0] == '\0') return 0;

    /* Verify that ffmpeg supports libsoxr for
     * aresample=resampler=soxr. Many FFmpeg builds do not print "soxr"
     * in `-filters` output, so this cannot rely on plain filter list text. */
    char* esc_ffmpeg = platform_escape_path_for_command(ffmpeg);
    if (!esc_ffmpeg) return 0;

    {
        char cmd[2048];
        FILE* fp;
        char line[512];
        int found_soxr = 0;

        snprintf(cmd, sizeof(cmd),
                 "%s -hide_banner -h filter=aresample 2>/dev/null",
                 esc_ffmpeg);

        fp = popen(cmd, "r");
        if (fp) {
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "soxr")) {
                    found_soxr = 1;
                    break;
                }
            }
            pclose(fp);
        }

        if (found_soxr) {
            free(esc_ffmpeg);
            return 1;
        }
    }

    /* Fallback probe: execute a tiny graph that requires soxr. */
    {
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
                 "%s -hide_banner -v error -f lavfi -i anullsrc=r=48000:cl=stereo "
                 "-t 0.01 -af aresample=resampler=soxr -f null - 2>/dev/null",
                 esc_ffmpeg);
        int result = (system(cmd) == 0) ? 1 : 0;
        free(esc_ffmpeg);
        return result;
    }
}

int platform_supports_codec(const char* codec) {
    if (!codec) return 0;

    /* Cross-platform codecs */
    if (strcmp(codec, "copy")      == 0 ||
        strcmp(codec, "prores")    == 0 ||
        strcmp(codec, "prores_ks") == 0)
        return 1;

    /* macOS VideoToolbox codecs */
    if (strcmp(codec, "hevc_videotoolbox")  == 0 ||
        strcmp(codec, "h264_videotoolbox")  == 0 ||
        strcmp(codec, "prores_videotoolbox") == 0)
        return 1;

    /* Linux / Windows platform-specific codecs are not supported on macOS */
    return 0;
}

const char* platform_get_video_codec_flags(const char* codec,
                                            const char* input_path,
                                            const void* opts_void) {
    if (!codec) return NULL;

    /* Use a persistent buffer — valid until the next call */
    static char flags[512];
    flags[0] = '\0';

    if (strcmp(codec, "hevc_videotoolbox") == 0) {
        int w = 0, h = 0;
        double fps = 0.0;
        if (input_path && input_path[0] != '\0')
            macos_get_video_info(input_path, &w, &h, &fps);
        int bitrate = macos_calc_hevc_vt_bitrate_kbps(w, h, fps);
        snprintf(flags, sizeof(flags),
                 "-c:v hevc_videotoolbox -b:v %dk -tag:v hvc1 -spatial_aq 1 ",
                 bitrate > 0 ? bitrate : 35000);
        return flags;
    }

    if (strcmp(codec, "h264_videotoolbox") == 0) {
        int w = 0, h = 0;
        double fps = 0.0;
        if (input_path && input_path[0] != '\0')
            macos_get_video_info(input_path, &w, &h, &fps);
        int bitrate = macos_calc_h264_vt_bitrate_kbps(w, h, fps);
        snprintf(flags, sizeof(flags),
                 "-c:v h264_videotoolbox -b:v %dk -spatial_aq 1 ",
                 bitrate > 0 ? bitrate : 50000);
        return flags;
    }

    if (strcmp(codec, "prores_videotoolbox") == 0) {
        int profile = 2;  /* default: standard */
        if (opts_void) {
            const ConvertOptions* copts = (const ConvertOptions*)opts_void;
            /* Convert preset string to profile number */
            if (copts->preset[0] != '\0') {
                if (strcmp((const char*)copts->preset, "lt") == 0) profile = 1;
                else if (strcmp((const char*)copts->preset, "hq") == 0) profile = 3;
                else if (strcmp((const char*)copts->preset, "4444") == 0) profile = 4;
                else profile = 2;  /* standard or default */
            }
        }
        snprintf(flags, sizeof(flags),
                 "-c:v prores_videotoolbox -profile:v %d -allow_sw 1 ",
                 profile);
        return flags;
    }

    /* Not a macOS platform-specific codec */
    return NULL;
}

int platform_detect_gpu_support(void) {
    /* VideoToolbox is always available on modern macOS */
    return PLAT_CAP_VIDEOTOOLBOX | PLAT_CAP_AAC_AT;
}

int platform_get_hw_device_for_codec(const char* codec,
                                     char* hw_device,
                                     size_t hw_device_sz) {
    (void)codec;
    (void)hw_device;
    (void)hw_device_sz;
    /* macOS VideoToolbox uses an implicit system device — no explicit
     * hw_device string is needed. */
    return 0;
}

/* ---------------------------------------------------------------
 *  Utilities
 * --------------------------------------------------------------- */

int platform_get_cpu_count(void) {
    int count = 0;
    size_t size = sizeof(count);
    if (sysctlbyname("hw.ncpu", &count, &size, NULL, 0) == 0 && count > 0)
        return count;
    /* Fallback */
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) return 1;
    return (int)n;
}

int platform_get_video_info(const char* input_path,
                             int* width, int* height, double* fps) {
    if (!input_path || !width || !height || !fps) return 0;
    macos_get_video_info(input_path, width, height, fps);
    return (*width > 0 && *height > 0 && *fps > 0.0) ? 1 : 0;
}

const char* platform_get_preinput_hw_flags(const char* codec, const void* opts) {
    (void)codec; (void)opts;
    /* VideoToolbox uses implicit system device — no pre-input flags needed */
    return NULL;
}

const char* platform_get_hw_vfilter(const char* codec, const void* opts) {
    (void)codec; (void)opts;
    /* VideoToolbox accepts CPU-decoded frames directly — no hwupload filter */
    return NULL;
}
