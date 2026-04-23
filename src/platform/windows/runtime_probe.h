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

    /* AMD AMF */
    int has_h264_amf;
    int has_hevc_amf;

    /* Intel QSV */
    int has_h264_qsv;
    int has_hevc_qsv;
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
