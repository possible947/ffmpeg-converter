#ifndef LINUX_RUNTIME_PROBE_H
#define LINUX_RUNTIME_PROBE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* VAAPI (Intel/AMD iGPU via DRI render node) */
    int has_h264_vaapi;
    int has_hevc_vaapi;
    int has_av1_vaapi;
    int has_hevc_vaapi_10bit;
    int has_av1_vaapi_10bit;
    char default_render_node[1024];

    /* NVIDIA NVENC */
    int has_h264_nvenc;
    int has_hevc_nvenc;
    int has_av1_nvenc;
    int has_hevc_nvenc_10bit;
    int has_av1_nvenc_10bit;

    /* AMD AMF */
    int has_h264_amf;
    int has_hevc_amf;
    int has_av1_amf;    /* av1_amf — requires RDNA3+ (RX 7000 series) */

    /* Intel QSV */
    int has_h264_qsv;
    int has_hevc_qsv;
    int has_av1_qsv;
    int has_hevc_qsv_10bit;
    int has_av1_qsv_10bit;

    /* Vulkan (GPU-accelerated ProRes via Vulkan) */
    int has_prores_ks_vulkan;
    int vulkan_working_mask;   /* bit N = 1 if vk:N passed the probe */
    int vulkan_device_index;   /* recommended default (highest working index) */
    int vulkan_device_count;   /* total working Vulkan devices found */

    /* Vulkan hardware video encoders (h264_vulkan/hevc_vulkan/av1_vulkan) */
    int has_h264_vulkan;
    int has_hevc_vulkan;
    int has_av1_vulkan;
    int vulkan_hw_working_mask;  /* bit N = 1 if vk:N passed the hw-encode probe */
    int vulkan_hw_device_index;  /* recommended default (highest working index) */
    int vulkan_hw_device_count;  /* total working Vulkan hw-encode devices found */

    /* Binary paths */
    char ffmpeg_bin[1024];
    char ffprobe_bin[1024];
    char mkvmerge_bin[1024];
    char mp4box_bin[1024];
    int using_bundled_ffmpeg;
    int using_bundled_ffprobe;
    int using_bundled_mkvmerge;
    int using_bundled_mp4box;
} LinuxCodecSupport;

int linux_probe_codec_support(LinuxCodecSupport *out_support);
int linux_probe_catalog_component_enabled(const char *catalog_path,
                                          const char *platform,
                                          const char *group,
                                          const char *encoder,
                                          const char *final_codec);
int linux_is_bundled_ffmpeg_available(void);
int linux_is_bundled_ffprobe_available(void);
int linux_is_bundled_mkvmerge_available(void);
int linux_is_bundled_mp4box_available(void);
const char *linux_get_preferred_ffmpeg_bin(void);
const char *linux_get_preferred_ffprobe_bin(void);
const char *linux_get_preferred_mkvmerge_bin(void);
const char *linux_get_preferred_mp4box_bin(void);

/**
 * Get a friendly name for a VAAPI render device.
 * Attempts to read device name from sysfs, falls back to device node name.
 * @param device_path Full path like "/dev/dri/renderD128"
 * @param out_name Output buffer for friendly name (e.g., "Intel UHD 630")
 * @param out_sz Size of output buffer
 * @return 0 on success, -1 on error; out_name is always null-terminated
 */
int linux_get_vaapi_device_name(const char *device_path, char *out_name, size_t out_sz);

#ifdef __cplusplus
}
#endif

#endif