/**
 * runtime_probe.h (macOS)
 * Platform-specific GPU codec support structure for macOS.
 * macOS uses VideoToolbox (VT) for hardware-accelerated encoding.
 */

#ifndef MACOS_RUNTIME_PROBE_H
#define MACOS_RUNTIME_PROBE_H

#include "../runtime_probe_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    RuntimeProbeBinaries bins;  /* must be first */

    /* Apple VideoToolbox encoders */
    int has_h264_videotoolbox;
    int has_hevc_videotoolbox;
} MacosCodecSupport;

/* macOS-specific public API */
int         macos_probe_codec_support(MacosCodecSupport *out_support);
int         macos_is_bundled_ffmpeg_available(void);
int         macos_is_bundled_ffprobe_available(void);
int         macos_is_bundled_mkvmerge_available(void);
int         macos_is_bundled_mp4box_available(void);
const char *macos_get_preferred_ffmpeg_bin(void);
const char *macos_get_preferred_ffprobe_bin(void);
const char *macos_get_preferred_mkvmerge_bin(void);
const char *macos_get_preferred_mp4box_bin(void);

#ifdef __cplusplus
}
#endif

#endif /* MACOS_RUNTIME_PROBE_H */
