#include "mux_policy.h"

#include <stdio.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", message);
}

int mux_build_profile_resolve(const char *platform,
                              const char *ffmpeg_version,
                              MuxBuildProfile *profile)
{
    if (!platform || !ffmpeg_version || !profile)
        return 0;

    if (strcmp(platform, "linux") == 0)
        profile->platform = MUX_PLATFORM_LINUX;
    else if (strcmp(platform, "windows") == 0)
        profile->platform = MUX_PLATFORM_WINDOWS;
    else if (strcmp(platform, "macos") == 0)
        profile->platform = MUX_PLATFORM_MACOS;
    else
        return 0;

    if (strcmp(ffmpeg_version, "8.1") == 0)
        profile->ffmpeg_version = MUX_FFMPEG_8_1;
    else if (strcmp(ffmpeg_version, "9.0") == 0)
        profile->ffmpeg_version = MUX_FFMPEG_9_0;
    else
        return 0;

    return 1;
}

int mux_build_profile_default(MuxBuildProfile *profile)
{
#if defined(_WIN32)
    const char *platform = "windows";
#elif defined(__APPLE__)
    const char *platform = "macos";
#else
    const char *platform = "linux";
#endif
#ifdef FFMPEG_CONVERTER_FFMPEG_PROFILE
    const char *version = FFMPEG_CONVERTER_FFMPEG_PROFILE;
#else
    const char *version = "8.1";
#endif
    return mux_build_profile_resolve(platform, version, profile);
}

MuxMode mux_mode_parse(const char *value)
{
    if (!value)
        return MUX_MODE_INVALID;
    if (strcmp(value, "copy") == 0)
        return MUX_MODE_COPY;
    if (strcmp(value, "mkv") == 0)
        return MUX_MODE_MKV;
    if (strcmp(value, "mov") == 0)
        return MUX_MODE_MOV;
    if (strcmp(value, "m4v") == 0)
        return MUX_MODE_M4V;
    return MUX_MODE_INVALID;
}

MuxAudioMode mux_audio_mode_parse(const char *value)
{
    if (!value)
        return MUX_AUDIO_INVALID;
    if (strcmp(value, "copy") == 0)
        return MUX_AUDIO_COPY;
    if (strcmp(value, "pcm") == 0)
        return MUX_AUDIO_PCM;
    if (strcmp(value, "fdk_aac_320") == 0 || strcmp(value, "aac") == 0)
        return MUX_AUDIO_AAC;
    if (strcmp(value, "fdk_aac_320_ac3_640") == 0 || strcmp(value, "aac+ac3") == 0)
        return MUX_AUDIO_AAC_AC3;
    return MUX_AUDIO_INVALID;
}

MuxVideoCodec mux_video_codec_parse(const char *value)
{
    if (!value)
        return MUX_VIDEO_UNKNOWN;
    if (strstr(value, "prores"))
        return MUX_VIDEO_PRORES;
    if (strstr(value, "h264") || strstr(value, "avc"))
        return MUX_VIDEO_H264;
    if (strstr(value, "hevc") || strstr(value, "h265"))
        return MUX_VIDEO_HEVC;
    return MUX_VIDEO_UNSUPPORTED;
}

int mux_policy_resolve(const MuxBuildProfile *profile,
                       MuxMode mode,
                       const char *audio_mode,
                       MuxVideoCodec source_video_codec,
                       MuxVideoCodec replacement_video_codec,
                       MuxPolicy *policy,
                       char *error,
                       size_t error_size)
{
    MuxAudioMode resolved_audio;
    MuxContainer container;
    int defaulted = 0;

    if (error && error_size)
        error[0] = '\0';

    if (!profile || !policy ||
        profile->platform < MUX_PLATFORM_LINUX ||
        profile->platform > MUX_PLATFORM_MACOS ||
        profile->ffmpeg_version < MUX_FFMPEG_8_1 ||
        profile->ffmpeg_version > MUX_FFMPEG_9_0 ||
        mode < MUX_MODE_COPY || mode >= MUX_MODE_INVALID) {
        set_error(error, error_size, "invalid mux profile or mode");
        return 0;
    }

    switch (mode) {
        case MUX_MODE_COPY:
            container = source_video_codec == MUX_VIDEO_PRORES
                            ? MUX_CONTAINER_MOV : MUX_CONTAINER_MKV;
            break;
        case MUX_MODE_MKV:
            container = MUX_CONTAINER_MKV;
            break;
        case MUX_MODE_MOV:
            container = MUX_CONTAINER_MOV;
            break;
        case MUX_MODE_M4V:
            container = MUX_CONTAINER_M4V;
            break;
        default:
            set_error(error, error_size, "unsupported mux mode");
            return 0;
    }

    if (!audio_mode || audio_mode[0] == '\0') {
        defaulted = 1;
        if (mode == MUX_MODE_M4V)
            resolved_audio = MUX_AUDIO_AAC_AC3;
        else
            resolved_audio = MUX_AUDIO_PCM;
    } else {
        resolved_audio = mux_audio_mode_parse(audio_mode);
    }

    if (resolved_audio == MUX_AUDIO_INVALID) {
        set_error(error, error_size, "unknown mux audio mode");
        return 0;
    }

    if (container == MUX_CONTAINER_MOV && resolved_audio != MUX_AUDIO_PCM) {
        set_error(error, error_size, "MOV mux mode supports PCM audio only");
        return 0;
    }
    if (container == MUX_CONTAINER_M4V &&
        resolved_audio != MUX_AUDIO_AAC && resolved_audio != MUX_AUDIO_AAC_AC3) {
        set_error(error, error_size, "M4V mux mode supports AAC or AAC+AC3 audio only");
        return 0;
    }

    if (mode == MUX_MODE_M4V &&
        replacement_video_codec != MUX_VIDEO_UNKNOWN &&
        replacement_video_codec != MUX_VIDEO_H264 &&
        replacement_video_codec != MUX_VIDEO_HEVC) {
        set_error(error, error_size, "M4V replacement video must be H.264 or HEVC");
        return 0;
    }
    if ((mode == MUX_MODE_MKV || mode == MUX_MODE_MOV) &&
        replacement_video_codec == MUX_VIDEO_UNSUPPORTED) {
        set_error(error, error_size, "unsupported replacement video codec");
        return 0;
    }

    memset(policy, 0, sizeof(*policy));
    policy->mode = mode;
    policy->container = container;
    policy->audio_mode = resolved_audio;
    policy->audio_track_policy = resolved_audio == MUX_AUDIO_COPY
                                     ? MUX_AUDIO_TRACK_ALL : MUX_AUDIO_TRACK_FIRST;
    policy->audio_mode_was_defaulted = defaulted;
    policy->requires_add_track = mode != MUX_MODE_COPY;
    policy->requires_video_replacement = mode != MUX_MODE_COPY;
    policy->preserve_video = 1;
    policy->preserve_chapters = 1;
    policy->preserve_metadata = 1;
    policy->preserve_subtitles_when_supported = 1;
    policy->video_action = MUX_STREAM_PRESERVE;
    policy->audio_action = resolved_audio == MUX_AUDIO_COPY
                               ? MUX_STREAM_PRESERVE : MUX_STREAM_CONVERT;
    policy->subtitle_action = MUX_STREAM_PRESERVE;
    policy->chapter_action = MUX_STREAM_PRESERVE;
    policy->metadata_action = MUX_STREAM_PRESERVE;
    policy->language_action = MUX_STREAM_PRESERVE;
    policy->disposition_action = MUX_STREAM_PRESERVE;
    policy->attachment_action = container == MUX_CONTAINER_MKV
                                    ? MUX_STREAM_PRESERVE : MUX_STREAM_DROP;
    return 1;
}

int mux_policy_resolve_hq(const MuxBuildProfile *profile,
                          MuxVideoCodec replacement_video_codec,
                          const char *audio_mode,
                          MuxPolicy *policy,
                          char *error,
                          size_t error_size)
{
    if (replacement_video_codec == MUX_VIDEO_UNKNOWN ||
        replacement_video_codec == MUX_VIDEO_UNSUPPORTED) {
        set_error(error, error_size, "unsupported HQ replacement video codec");
        return 0;
    }
    if (replacement_video_codec == MUX_VIDEO_PRORES)
        return mux_policy_resolve(profile, MUX_MODE_MOV, "pcm",
                                  MUX_VIDEO_UNKNOWN, replacement_video_codec,
                                  policy, error, error_size);
    return mux_policy_resolve(profile, MUX_MODE_MKV, audio_mode,
                              MUX_VIDEO_UNKNOWN, replacement_video_codec,
                              policy, error, error_size);
}

size_t mux_policy_list_audio_modes(const MuxBuildProfile *profile,
                                   MuxMode mode,
                                   const MuxVideoCodec *source_video_codecs,
                                   size_t source_count,
                                   MuxVideoCodec replacement_video_codec,
                                   MuxAudioMode *audio_modes,
                                   size_t audio_modes_capacity)
{
    static const MuxAudioMode candidates[] = {
        MUX_AUDIO_COPY, MUX_AUDIO_PCM, MUX_AUDIO_AAC, MUX_AUDIO_AAC_AC3
    };
    MuxAudioMode ordered[sizeof(candidates) / sizeof(candidates[0])];
    MuxPolicy default_policy;
    MuxVideoCodec unknown = MUX_VIDEO_UNKNOWN;
    const MuxVideoCodec *sources = source_count ? source_video_codecs : &unknown;
    size_t count = source_count ? source_count : 1;
    size_t ordered_count = 0;
    size_t result_count = 0;
    size_t i;
    size_t j;
    char error[128];

    if (!profile || (source_count && !source_video_codecs) ||
        !mux_policy_resolve(profile, mode, NULL, sources[0],
                            replacement_video_codec, &default_policy,
                            error, sizeof(error)))
        return 0;
    ordered[ordered_count++] = default_policy.audio_mode;
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (candidates[i] != default_policy.audio_mode)
            ordered[ordered_count++] = candidates[i];
    }

    for (i = 0; i < ordered_count; ++i) {
        int valid = 1;
        const char *audio = mux_audio_mode_name(ordered[i]);
        for (j = 0; j < count; ++j) {
            MuxPolicy policy;
            if (!mux_policy_resolve(profile, mode, audio, sources[j],
                                    replacement_video_codec, &policy,
                                    error, sizeof(error))) {
                valid = 0;
                break;
            }
        }
        if (valid) {
            if (audio_modes && result_count < audio_modes_capacity)
                audio_modes[result_count] = ordered[i];
            ++result_count;
        }
    }
    return result_count;
}

const char *mux_mode_name(MuxMode mode)
{
    switch (mode) {
        case MUX_MODE_COPY: return "copy";
        case MUX_MODE_MKV: return "mkv";
        case MUX_MODE_MOV: return "mov";
        case MUX_MODE_M4V: return "m4v";
        default: return "invalid";
    }
}

const char *mux_container_name(MuxContainer container)
{
    switch (container) {
        case MUX_CONTAINER_MKV: return "mkv";
        case MUX_CONTAINER_MOV: return "mov";
        case MUX_CONTAINER_M4V: return "m4v";
        default: return "invalid";
    }
}

const char *mux_container_extension(MuxContainer container)
{
    switch (container) {
        case MUX_CONTAINER_MKV: return "mkv";
        case MUX_CONTAINER_MOV: return "mov";
        case MUX_CONTAINER_M4V: return "m4v";
        default: return NULL;
    }
}

const char *mux_audio_mode_name(MuxAudioMode mode)
{
    switch (mode) {
        case MUX_AUDIO_COPY: return "copy";
        case MUX_AUDIO_PCM: return "pcm";
        case MUX_AUDIO_AAC: return "fdk_aac_320";
        case MUX_AUDIO_AAC_AC3: return "fdk_aac_320_ac3_640";
        default: return "invalid";
    }
}

MuxSubtitlePolicy mux_subtitle_policy_resolve(const char *codec,
                                              MuxContainer container)
{
    static const struct {
        const char *codec;
        const char *mkv_encoder;
        const char *mov_encoder;
    } compatibility[] = {
        {"mov_text", "srt", "copy"},
        {"subrip", "copy", "mov_text"},
        {"ass", "copy", "mov_text"},
        {"ssa", "copy", "mov_text"},
        {"webvtt", "copy", "mov_text"},
        {"hdmv_pgs_subtitle", "copy", NULL},
        {"dvd_subtitle", "copy", NULL},
        {"dvb_subtitle", "copy", NULL}
    };
    MuxSubtitlePolicy result = {MUX_SUBTITLE_DROP, NULL};
    size_t i;

    if (!codec || container < MUX_CONTAINER_MKV || container > MUX_CONTAINER_M4V)
        return result;
    for (i = 0; i < sizeof(compatibility) / sizeof(compatibility[0]); ++i) {
        if (strcmp(codec, compatibility[i].codec) == 0) {
            result.encoder = container == MUX_CONTAINER_MKV
                                 ? compatibility[i].mkv_encoder
                                 : compatibility[i].mov_encoder;
            if (result.encoder)
                result.action = strcmp(result.encoder, "copy") == 0
                                    ? MUX_SUBTITLE_COPY : MUX_SUBTITLE_CONVERT;
            break;
        }
    }
    return result;
}
