/**
 * runtime_probe.h (Windows)
 * Platform-specific GPU codec support structure for Windows.
 * Windows uses NVENC (NVIDIA), AMD AMF, and Intel QSV.
 */

#ifndef WINDOWS_RUNTIME_PROBE_H
#define WINDOWS_RUNTIME_PROBE_H

#include "../runtime_probe_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    RuntimeProbeBinaries bins;  /* must be first */

    /* NVIDIA NVENC */
    int has_h264_nvenc;
    int has_hevc_nvenc;
    int has_av1_nvenc;  /* av1_nvenc — requires Ada Lovelace (RTX 40-series+) */

    /* AMD AMF */
    int has_h264_amf;
    int has_hevc_amf;
    int has_av1_amf;    /* av1_amf — requires RDNA3+ (RX 7000 series) */

    /* Intel QSV */
    int has_h264_qsv;
    int has_hevc_qsv;
    int has_av1_qsv;    /* av1_qsv — requires Xe-HPG/Arc or 12th-gen+ iGPU */

    /* Vulkan (GPU-accelerated ProRes via Vulkan) */
    int has_prores_ks_vulkan;
    int vulkan_working_mask;   /* bit N = 1 if vk:N passed the probe */
    int vulkan_device_index;   /* recommended default (highest working index) */
    int vulkan_device_count;   /* total working Vulkan devices found */

    /* Vulkan hardware video encoders (h264_vulkan/hevc_vulkan/av1_vulkan) */
    int has_h264_vulkan;
    int has_hevc_vulkan;
    int has_av1_vulkan;
    int vulkan_hw_working_mask;
    int vulkan_hw_device_index;
    int vulkan_hw_device_count;
} WindowsCodecSupport;

/* Windows-specific public API */
int         windows_probe_codec_support(WindowsCodecSupport *out_support);
int         windows_is_bundled_ffmpeg_available(void);
int         windows_is_bundled_ffprobe_available(void);
int         windows_is_bundled_mkvmerge_available(void);
int         windows_is_bundled_mp4box_available(void);
const char *windows_get_preferred_ffmpeg_bin(void);
const char *windows_get_preferred_ffprobe_bin(void);
const char *windows_get_preferred_mkvmerge_bin(void);
const char *windows_get_preferred_mp4box_bin(void);

#ifdef __cplusplus
}
#endif

#endif /* WINDOWS_RUNTIME_PROBE_H */
