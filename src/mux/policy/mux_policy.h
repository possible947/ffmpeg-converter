#ifndef FFMPEG_CONVERTER_MUX_POLICY_H
#define FFMPEG_CONVERTER_MUX_POLICY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MUX_PLATFORM_LINUX = 0,
    MUX_PLATFORM_WINDOWS,
    MUX_PLATFORM_MACOS
} MuxPlatform;

typedef enum {
    MUX_FFMPEG_8_1 = 0,
    MUX_FFMPEG_9_0
} MuxFfmpegVersion;

typedef struct {
    MuxPlatform platform;
    MuxFfmpegVersion ffmpeg_version;
} MuxBuildProfile;

typedef enum {
    MUX_MODE_COPY = 0,
    MUX_MODE_MKV,
    MUX_MODE_MOV,
    MUX_MODE_M4V,
    MUX_MODE_INVALID
} MuxMode;

typedef enum {
    MUX_CONTAINER_MKV = 0,
    MUX_CONTAINER_MOV,
    MUX_CONTAINER_M4V
} MuxContainer;

typedef enum {
    MUX_AUDIO_COPY = 0,
    MUX_AUDIO_PCM,
    MUX_AUDIO_AAC,
    MUX_AUDIO_AAC_AC3,
    MUX_AUDIO_INVALID
} MuxAudioMode;

typedef enum {
    MUX_AUDIO_TRACK_FIRST = 0,
    MUX_AUDIO_TRACK_ALL
} MuxAudioTrackPolicy;

typedef enum {
    MUX_VIDEO_UNKNOWN = 0,
    MUX_VIDEO_PRORES,
    MUX_VIDEO_H264,
    MUX_VIDEO_HEVC,
    MUX_VIDEO_UNSUPPORTED
} MuxVideoCodec;

typedef enum {
    MUX_STREAM_PRESERVE = 0,
    MUX_STREAM_CONVERT,
    MUX_STREAM_DROP,
    MUX_STREAM_REJECT
} MuxStreamAction;

typedef enum {
    MUX_SUBTITLE_COPY = 0,
    MUX_SUBTITLE_CONVERT,
    MUX_SUBTITLE_DROP
} MuxSubtitleAction;

typedef struct {
    MuxSubtitleAction action;
    const char *encoder;
} MuxSubtitlePolicy;

typedef struct {
    MuxMode mode;
    MuxContainer container;
    MuxAudioMode audio_mode;
    MuxAudioTrackPolicy audio_track_policy;
    int audio_mode_was_defaulted;
    int requires_add_track;
    int requires_video_replacement;
    int preserve_video;
    int preserve_chapters;
    int preserve_metadata;
    int preserve_subtitles_when_supported;
    MuxStreamAction video_action;
    MuxStreamAction audio_action;
    MuxStreamAction subtitle_action;
    MuxStreamAction chapter_action;
    MuxStreamAction metadata_action;
    MuxStreamAction language_action;
    MuxStreamAction disposition_action;
    MuxStreamAction attachment_action;
} MuxPolicy;

/* Parse canonical profile values ("linux"/"windows"/"macos", "8.1"/"9.0"). */
int mux_build_profile_resolve(const char *platform,
                              const char *ffmpeg_version,
                              MuxBuildProfile *profile);
int mux_build_profile_default(MuxBuildProfile *profile);

MuxMode mux_mode_parse(const char *value);
MuxAudioMode mux_audio_mode_parse(const char *value);
MuxVideoCodec mux_video_codec_parse(const char *value);

/*
 * Resolve and validate a mode/audio/video tuple without performing I/O.
 * A NULL audio mode requests the mode's canonical default. For copy mode,
 * source_video_codec selects MOV for ProRes and MKV otherwise. Replacement
 * modes require a supported replacement_video_codec.
 */
int mux_policy_resolve(const MuxBuildProfile *profile,
                       MuxMode mode,
                       const char *audio_mode,
                       MuxVideoCodec source_video_codec,
                       MuxVideoCodec replacement_video_codec,
                       MuxPolicy *policy,
                       char *error,
                       size_t error_size);
int mux_policy_resolve_hq(const MuxBuildProfile *profile,
                          MuxVideoCodec replacement_video_codec,
                          const char *audio_mode,
                          MuxPolicy *policy,
                          char *error,
                          size_t error_size);
size_t mux_policy_list_audio_modes(const MuxBuildProfile *profile,
                                   MuxMode mode,
                                   const MuxVideoCodec *source_video_codecs,
                                   size_t source_count,
                                   MuxVideoCodec replacement_video_codec,
                                   MuxAudioMode *audio_modes,
                                   size_t audio_modes_capacity);

const char *mux_mode_name(MuxMode mode);
const char *mux_container_name(MuxContainer container);
const char *mux_container_extension(MuxContainer container);
const char *mux_audio_mode_name(MuxAudioMode mode);
MuxSubtitlePolicy mux_subtitle_policy_resolve(const char *codec,
                                              MuxContainer container);

#ifdef __cplusplus
}
#endif

#endif
