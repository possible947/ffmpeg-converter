#ifndef LINUX_RUNTIME_PROBE_H
#define LINUX_RUNTIME_PROBE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int has_h264_vaapi;
    int has_hevc_vaapi;
    char default_render_node[1024];
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
int linux_is_bundled_ffmpeg_available(void);
int linux_is_bundled_ffprobe_available(void);
int linux_is_bundled_mkvmerge_available(void);
int linux_is_bundled_mp4box_available(void);
const char *linux_get_preferred_ffmpeg_bin(void);
const char *linux_get_preferred_ffprobe_bin(void);
const char *linux_get_preferred_mkvmerge_bin(void);
const char *linux_get_preferred_mp4box_bin(void);

#ifdef __cplusplus
}
#endif

#endif