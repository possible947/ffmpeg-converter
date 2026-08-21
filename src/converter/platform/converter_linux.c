/* platform/converter_linux.c
 * Linux-specific implementations of the converter platform abstraction.
 * Wraps linux/runtime_probe.c for binary resolution and GPU detection.
 */

#include "../converter_platform.h"
#include "../converter.h"
/* runtime_probe.h is found via CMake target_include_directories */
#include "linux/runtime_probe.h"
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* ---------------------------------------------------------------
 *  Lifecycle
 * --------------------------------------------------------------- */

int platform_init(void) {
    /* No heavy initialisation needed on Linux.
     * Binary resolution and GPU detection are done lazily via runtime_probe. */
    return 0;
}

void platform_cleanup(void) {
    /* Nothing to release on Linux. */
}

/* ---------------------------------------------------------------
 *  Binary resolution
 * --------------------------------------------------------------- */

const char* platform_get_ffmpeg_bin(void) {
    const char* v = getenv("FFMPEG");
    if (v && v[0] != '\0') return v;
    v = getenv("FFMPEG_BIN");
    if (v && v[0] != '\0') return v;
    return linux_get_preferred_ffmpeg_bin();
}

const char* platform_get_ffprobe_bin(void) {
    const char* v = getenv("FFPROBE");
    if (v && v[0] != '\0') return v;
    v = getenv("FFPROBE_BIN");
    if (v && v[0] != '\0') return v;
    return linux_get_preferred_ffprobe_bin();
}

const char* platform_get_mkvmerge_bin(void) {
    const char* v = getenv("MKVMERGE");
    if (v && v[0] != '\0') return v;
    return linux_get_preferred_mkvmerge_bin();
}

const char* platform_get_mp4box_bin(void) {
    const char* v = getenv("MP4BOX");
    if (v && v[0] != '\0') return v;
    return linux_get_preferred_mp4box_bin();
}

/* ---------------------------------------------------------------
 *  Path operations
 * --------------------------------------------------------------- */

char* platform_escape_path_for_command(const char* path) {
    if (!path) return NULL;

    /* Worst case: every character is a single-quote → replace with '\''\' */
    size_t in_len = strlen(path);
    /* 2 (outer quotes) + 4 * in_len (each ' → '\'' = 4 chars) + 1 (NUL) */
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
    /* dir + "/" + file + NUL */
    char* out = malloc(dir_len + 1 + file_len + 1);
    if (!out) return NULL;
    memcpy(out, dir, dir_len);
    out[dir_len] = '/';
    memcpy(out + dir_len + 1, file, file_len + 1);  /* +1 for NUL */
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
    (void)line;  /* no-op: Linux ffmpeg outputs \n only */
}

/* ---------------------------------------------------------------
 *  Audio and GPU support
 * --------------------------------------------------------------- */

int platform_validate_audio_filters(void) {
    /* Check that ffmpeg was built with --enable-libsoxr.
     * On many FFmpeg builds, `soxr` is not listed in `-filters` output;
     * it is exposed as an `aresample` option. */
    const char* ffmpeg = platform_get_ffmpeg_bin();
    if (!ffmpeg || ffmpeg[0] == '\0') return 0;

    char* esc_ffmpeg = platform_escape_path_for_command(ffmpeg);
    if (!esc_ffmpeg) return 0;

    {
        char cmd[1024];
        FILE* fp;
        char line[512];
        int found_soxr = 0;

        snprintf(cmd, sizeof(cmd),
                 "%s -hide_banner -h filter=aresample 2>/dev/null",
                 esc_ffmpeg);

        fp = platform_popen(cmd, "r");
        if (fp) {
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "soxr")) {
                    found_soxr = 1;
                    break;
                }
            }
            platform_pclose(fp);
        }

        if (found_soxr) {
            free(esc_ffmpeg);
            return 1;
        }
    }

    /* Fallback probe: execute a tiny soxr resample graph. */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "%s -hide_banner -v error -f lavfi -i anullsrc=r=48000:cl=stereo "
             "-t 0.01 -af aresample=resampler=soxr -f null - 2>/dev/null",
             esc_ffmpeg);
    int result = (system(cmd) == 0) ? 1 : 0;
    free(esc_ffmpeg);
    return result;
}

int platform_supports_codec(const char* codec) {
    if (!codec) return 0;

    /* Cross-platform codecs — always supported */
    if (strcmp(codec, "copy")      == 0 ||
        strcmp(codec, "prores")    == 0 ||
        strcmp(codec, "prores_ks") == 0)
        return 1;

    /* GPU codecs — check via runtime_probe */
    {
        LinuxCodecSupport support;
        linux_probe_codec_support(&support);

        if (strcmp(codec, "h264_vaapi")       == 0) return support.has_h264_vaapi;
        if (strcmp(codec, "hevc_vaapi")       == 0) return support.has_hevc_vaapi;
        if (strcmp(codec, "h264_nvenc")       == 0) return support.has_h264_nvenc;
        if (strcmp(codec, "hevc_nvenc")       == 0) return support.has_hevc_nvenc;
        if (strcmp(codec, "h264_amf")         == 0) return support.has_h264_amf;
        if (strcmp(codec, "hevc_amf")         == 0) return support.has_hevc_amf;
        if (strcmp(codec, "av1_amf")          == 0) return support.has_av1_amf;
        if (strcmp(codec, "h264_qsv")         == 0) return support.has_h264_qsv;
        if (strcmp(codec, "hevc_qsv")         == 0) return support.has_hevc_qsv;
        if (strcmp(codec, "prores_ks_vulkan") == 0) return support.has_prores_ks_vulkan;
        if (strcmp(codec, "h264_vulkan")      == 0) return support.has_h264_vulkan;
        if (strcmp(codec, "hevc_vulkan")      == 0) return support.has_hevc_vulkan;
        if (strcmp(codec, "av1_vulkan")       == 0) return support.has_av1_vulkan;
    }

    return 0;
}

const char* platform_get_video_codec_flags(const char* codec,
                                            const char* input_path,
                                            const void* opts) {
    (void)input_path;

    const ConvertOptions* copt = (const ConvertOptions*)opts;
    static char prores_flags[256];

    if (!codec) return NULL;

    /* Speed/balance/quality preset tiers for GPU codecs (Phase 2).
     * `default` strings are byte-for-byte identical to pre-Phase-2 behavior —
     * zero regression for existing users/scripts. Values mirror presets.json
     * Section 5 (v3.0-Phase2.md) and must be kept in sync with it. */
    {
        typedef struct {
            const char* codec;
            const char* def;
            const char* speed;
            const char* balance;
            const char* quality;
        } HwPreset;

        static const HwPreset hw_presets[] = {
            { "h264_vaapi",
              "-c:v h264_vaapi -rc_mode auto ",
              "-c:v h264_vaapi -rc_mode CQP -qp 28 ",
              "-c:v h264_vaapi -rc_mode CQP -qp 24 ",
              "-c:v h264_vaapi -rc_mode CQP -qp 20 " },
            { "hevc_vaapi",
              "-c:v hevc_vaapi -rc_mode auto ",
              "-c:v hevc_vaapi -rc_mode CQP -qp 28 ",
              "-c:v hevc_vaapi -rc_mode CQP -qp 24 ",
              "-c:v hevc_vaapi -rc_mode CQP -qp 20 " },
            { "h264_nvenc",
              "-c:v h264_nvenc -preset p7 -qp 22 -spatial_aq 1 -temporal_aq 1 ",
              "-c:v h264_nvenc -preset p1 -qp 22 -spatial_aq 1 -temporal_aq 1 ",
              "-c:v h264_nvenc -preset p4 -qp 22 -spatial_aq 1 -temporal_aq 1 ",
              "-c:v h264_nvenc -preset p7 -qp 22 -spatial_aq 1 -temporal_aq 1 " },
            { "hevc_nvenc",
              "-c:v hevc_nvenc -preset hq -cq 25 -lookahead_level auto ",
              "-c:v hevc_nvenc -preset p1 -cq 25 -lookahead_level 0 ",
              "-c:v hevc_nvenc -preset p4 -cq 25 -lookahead_level auto ",
              "-c:v hevc_nvenc -preset p7 -cq 25 -lookahead_level auto " },
            { "h264_amf",
              "-c:v h264_amf ",
              "-c:v h264_amf -quality speed ",
              "-c:v h264_amf -quality balanced ",
              "-c:v h264_amf -quality quality " },
            { "hevc_amf",
              "-c:v hevc_amf ",
              "-c:v hevc_amf -quality speed ",
              "-c:v hevc_amf -quality balanced ",
              "-c:v hevc_amf -quality quality " },
            { "av1_amf",
              "-c:v av1_amf ",
              "-c:v av1_amf -quality speed ",
              "-c:v av1_amf -quality balanced ",
              "-c:v av1_amf -quality quality " },
            { "h264_qsv",
              "-c:v h264_qsv -global_quality 22 -preset slower "
              "-look_ahead 1 -look_ahead_depth 40 -extbrc 1 ",
              "-c:v h264_qsv -global_quality 22 -preset veryfast -extbrc 1 ",
              "-c:v h264_qsv -global_quality 22 -preset medium "
              "-look_ahead 1 -look_ahead_depth 40 -extbrc 1 ",
              "-c:v h264_qsv -global_quality 22 -preset slower "
              "-look_ahead 1 -look_ahead_depth 40 -extbrc 1 " },
            { "hevc_qsv",
              "-c:v hevc_qsv -global_quality 25 -preset slow "
              "-g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1 ",
              "-c:v hevc_qsv -global_quality 25 -preset fast -g 240 -bf 4 ",
              "-c:v hevc_qsv -global_quality 25 -preset medium "
              "-g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1 ",
              "-c:v hevc_qsv -global_quality 25 -preset slow "
              "-g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1 " },
            { "h264_vulkan",
              "-c:v h264_vulkan -qp 18 ",
              "-c:v h264_vulkan -qp 28 ",
              "-c:v h264_vulkan -qp 23 ",
              "-c:v h264_vulkan -qp 18 " },
            { "hevc_vulkan",
              "-c:v hevc_vulkan -qp 18 ",
              "-c:v hevc_vulkan -qp 28 ",
              "-c:v hevc_vulkan -qp 23 ",
              "-c:v hevc_vulkan -qp 18 " },
            { "av1_vulkan",
              "-c:v av1_vulkan -qp 60 ",
              "-c:v av1_vulkan -qp 120 ",
              "-c:v av1_vulkan -qp 90 ",
              "-c:v av1_vulkan -qp 60 " },
        };
        size_t i;

        for (i = 0; i < sizeof(hw_presets) / sizeof(hw_presets[0]); i++) {
            if (strcmp(codec, hw_presets[i].codec) != 0)
                continue;
            if (copt && copt->preset[0] != '\0') {
                if (strcmp((const char*)copt->preset, "speed") == 0)
                    return hw_presets[i].speed;
                if (strcmp((const char*)copt->preset, "balance") == 0)
                    return hw_presets[i].balance;
                if (strcmp((const char*)copt->preset, "quality") == 0)
                    return hw_presets[i].quality;
            }
            return hw_presets[i].def;
        }
    }

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

    /* Not a Linux platform-specific codec */
    return NULL;
}

int platform_detect_gpu_support(void) {
    LinuxCodecSupport support;
    linux_probe_codec_support(&support);
    int caps = 0;
    if (support.has_h264_vaapi)       caps |= PLAT_CAP_VAAPI_H264;
    if (support.has_hevc_vaapi)       caps |= PLAT_CAP_VAAPI_HEVC;
    if (support.has_h264_nvenc)       caps |= PLAT_CAP_NVENC_H264;
    if (support.has_hevc_nvenc)       caps |= PLAT_CAP_NVENC_HEVC;
    if (support.has_h264_amf)         caps |= PLAT_CAP_AMF_H264;
    if (support.has_hevc_amf)         caps |= PLAT_CAP_AMF_HEVC;
    if (support.has_av1_amf)          caps |= PLAT_CAP_AMF_AV1;
    if (support.has_h264_qsv)         caps |= PLAT_CAP_QSV_H264;
    if (support.has_hevc_qsv)         caps |= PLAT_CAP_QSV_HEVC;
    if (support.has_prores_ks_vulkan) caps |= PLAT_CAP_VULKAN_PRORES;
    if (support.has_h264_vulkan)      caps |= PLAT_CAP_VULKAN_H264;
    if (support.has_hevc_vulkan)      caps |= PLAT_CAP_VULKAN_HEVC;
    if (support.has_av1_vulkan)       caps |= PLAT_CAP_VULKAN_AV1;

    /* Probe decoders: libdav1d (software AV1) and av1_qsv (Intel QSV).
     * libdav1d is a pure-software AV1 decoder with no hardware dependency —
     * it bypasses the native av1 decoder which can crash on systems where
     * the GPU does not support hardware AV1 decode (NVDEC/VAAPI). */
    {
        const char* ffmpeg = platform_get_ffmpeg_bin();
        if (ffmpeg && ffmpeg[0] != '\0') {
            char* esc_ffmpeg = platform_escape_path_for_command(ffmpeg);
            if (esc_ffmpeg) {
                char cmd[1024];
                snprintf(cmd, sizeof(cmd),
                         "%s -hide_banner -v error -decoders 2>/dev/null",
                         esc_ffmpeg);
                FILE* fp = platform_popen(cmd, "r");
                if (fp) {
                    char line[1024];
                    while (fgets(line, sizeof(line), fp)) {
                        if (strstr(line, " libdav1d"))
                            caps |= PLAT_CAP_LIBDAV1D_DEC;
                        if ((caps & (PLAT_CAP_QSV_H264 | PLAT_CAP_QSV_HEVC)) &&
                            strstr(line, " av1_qsv"))
                            caps |= PLAT_CAP_AV1_QSV_DEC;
                    }
                    platform_pclose(fp);
                }
                free(esc_ffmpeg);
            }
        }
    }

    return caps;
}

int platform_get_hw_device_for_codec(const char* codec,
                                     char* hw_device,
                                     size_t hw_device_sz) {
    LinuxCodecSupport support;

    if (!codec || !hw_device || hw_device_sz == 0)
        return 0;

    /* Only VAAPI codecs need a hardware device on Linux */
    if (strcmp(codec, "h264_vaapi") != 0 && strcmp(codec, "hevc_vaapi") != 0)
        return 0;

    linux_probe_codec_support(&support);

    if (support.default_render_node[0] != '\0') {
        strncpy(hw_device, support.default_render_node, hw_device_sz - 1);
        hw_device[hw_device_sz - 1] = '\0';
        return 1;
    }

    return 0;
}

/* ---------------------------------------------------------------
 *  Utilities
 * --------------------------------------------------------------- */

int platform_get_cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) return 1;
    return (int)n;
}

int platform_get_video_info(const char* input_path,
                             int* width, int* height, double* fps) {
    /* Not needed for Linux codecs (VAAPI does not require bitrate calculation) */
    (void)input_path;
    if (width)  *width  = 0;
    if (height) *height = 0;
    if (fps)    *fps    = 0.0;
    return 0;
}

const char* platform_get_preinput_hw_flags(const char* codec, const void* opts) {
    /* All *_vulkan codecs (compute-shader ProRes and hardware h264/hevc/av1)
     * share the same pre-input device init pattern and the same probed
     * --vk_device index. */
    if (codec &&
        (strcmp(codec, "prores_ks_vulkan") == 0 ||
         strcmp(codec, "h264_vulkan") == 0 ||
         strcmp(codec, "hevc_vulkan") == 0 ||
         strcmp(codec, "av1_vulkan") == 0)) {
        static char vk_flag[64];
        const ConvertOptions* copt = (const ConvertOptions*)opts;
        int vk_idx = (copt && copt->vulkan_device >= 0) ? copt->vulkan_device : 1;
        snprintf(vk_flag, sizeof(vk_flag),
                 "-init_hw_device vulkan=vk:%d -filter_hw_device vk", vk_idx);
        return vk_flag;
    }
    /* VAAPI uses the hw_device path (-vaapi_device) in converter.c */
    return NULL;
}

const char* platform_get_hw_vfilter(const char* codec, const void* opts) {
    if (codec &&
        (strcmp(codec, "h264_vaapi") == 0 || strcmp(codec, "hevc_vaapi") == 0 ||
         strcmp(codec, "h264_vulkan") == 0 || strcmp(codec, "hevc_vulkan") == 0 ||
         strcmp(codec, "av1_vulkan") == 0))
        return "nv12,hwupload";
    if (codec && strcmp(codec, "prores_ks_vulkan") == 0) {
        const ConvertOptions* copt = (const ConvertOptions*)opts;
        /* ProRes 4444 uses yuv444p10le; all other profiles use yuv422p10le */
        if (copt && copt->preset[0] != '\0' && strcmp((const char*)copt->preset, "4444") == 0)
            return "yuv444p10le,hwupload";
        return "yuv422p10le,hwupload";
    }
    return NULL;
}
