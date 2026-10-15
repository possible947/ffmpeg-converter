#include "mux_policy.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        failures++; \
    } \
} while (0)

static int resolve(MuxMode mode, const char *audio, MuxVideoCodec source,
                   MuxVideoCodec replacement, MuxPolicy *policy)
{
    MuxBuildProfile profile;
    char error[128];
    mux_build_profile_resolve("linux", "8.1", &profile);
    return mux_policy_resolve(&profile, mode, audio, source, replacement,
                              policy, error, sizeof(error));
}

static void test_subtitle_compatibility(void)
{
    MuxSubtitlePolicy subtitle;
    static const char *mkv_copy[] = {
        "subrip", "ass", "ssa", "webvtt", "dvd_subtitle",
        "hdmv_pgs_subtitle", "dvb_subtitle"
    };
    size_t i;
    subtitle = mux_subtitle_policy_resolve("mov_text", MUX_CONTAINER_MKV);
    CHECK(subtitle.action == MUX_SUBTITLE_CONVERT &&
          strcmp(subtitle.encoder, "srt") == 0,
          "MKV converts mov_text to SRT");
    for (i = 0; i < sizeof(mkv_copy) / sizeof(mkv_copy[0]); ++i) {
        subtitle = mux_subtitle_policy_resolve(mkv_copy[i], MUX_CONTAINER_MKV);
        CHECK(subtitle.action == MUX_SUBTITLE_COPY,
              "MKV copies supported text and bitmap subtitles");
    }
    subtitle = mux_subtitle_policy_resolve("subrip", MUX_CONTAINER_MOV);
    CHECK(subtitle.action == MUX_SUBTITLE_CONVERT &&
          strcmp(subtitle.encoder, "mov_text") == 0,
          "MOV converts SRT to mov_text");
    CHECK(mux_subtitle_policy_resolve("mov_text", MUX_CONTAINER_MOV).action ==
          MUX_SUBTITLE_COPY, "MOV copies mov_text");
    CHECK(mux_subtitle_policy_resolve("ass", MUX_CONTAINER_M4V).action ==
          MUX_SUBTITLE_CONVERT, "M4V converts text subtitles");
    CHECK(mux_subtitle_policy_resolve("hdmv_pgs_subtitle", MUX_CONTAINER_MOV).action ==
          MUX_SUBTITLE_DROP, "MOV drops unsupported bitmap subtitles");
    CHECK(mux_subtitle_policy_resolve("unknown", MUX_CONTAINER_MKV).action ==
          MUX_SUBTITLE_DROP, "unknown subtitle codecs are never copied blindly");
    CHECK(mux_subtitle_policy_resolve(NULL, MUX_CONTAINER_MKV).action ==
          MUX_SUBTITLE_DROP, "missing subtitle codecs are never copied blindly");
}

int main(void)
{
    MuxBuildProfile profile;
    MuxPolicy policy;
    char error[128];
    static const char *platforms[] = {"linux", "windows", "macos"};
    static const char *versions[] = {"8.1", "9.0"};
    size_t p;
    size_t v;

    test_subtitle_compatibility();
    CHECK(mux_build_profile_resolve("linux", "8.1", &profile),
          "resolve Linux/FFmpeg 8.1 profile");
    CHECK(profile.platform == MUX_PLATFORM_LINUX &&
          profile.ffmpeg_version == MUX_FFMPEG_8_1,
          "profile fields");
    CHECK(mux_build_profile_resolve("macos", "9.0", &profile),
          "resolve macOS/FFmpeg 9.0 profile");
    CHECK(!mux_build_profile_resolve("linux", "8.0", &profile),
          "reject unsupported FFmpeg version");
    CHECK(!mux_build_profile_resolve("darwin", "9.0", &profile),
          "reject noncanonical platform");
    for (p = 0; p < sizeof(platforms) / sizeof(platforms[0]); ++p) {
        for (v = 0; v < sizeof(versions) / sizeof(versions[0]); ++v) {
            CHECK(mux_build_profile_resolve(platforms[p], versions[v], &profile),
                  "resolve each supported platform/version profile");
            CHECK(mux_policy_resolve(&profile, MUX_MODE_M4V, "fdk_aac_320",
                                     MUX_VIDEO_UNKNOWN, MUX_VIDEO_H264,
                                     &policy, error, sizeof(error)),
                  "M4V AAC-only policy is available in each supported profile");
        }
    }

    CHECK(resolve(MUX_MODE_COPY, NULL, MUX_VIDEO_PRORES,
                  MUX_VIDEO_UNKNOWN, &policy) &&
          policy.container == MUX_CONTAINER_MOV &&
          !policy.requires_add_track,
          "copy mode routes ProRes to MOV without Add Track");
    CHECK(resolve(MUX_MODE_COPY, "copy", MUX_VIDEO_H264,
                  MUX_VIDEO_UNKNOWN, &policy) &&
          policy.container == MUX_CONTAINER_MKV &&
          policy.audio_mode == MUX_AUDIO_COPY,
          "copy mode routes non-ProRes to MKV and copies audio");
    CHECK(resolve(MUX_MODE_MKV, "fdk_aac_320_ac3_640", MUX_VIDEO_UNKNOWN,
                  MUX_VIDEO_PRORES, &policy) &&
          policy.requires_add_track && policy.container == MUX_CONTAINER_MKV,
          "MKV permits ProRes replacement and AAC+AC3");
    CHECK(resolve(MUX_MODE_MOV, "pcm", MUX_VIDEO_UNKNOWN,
                  MUX_VIDEO_H264, &policy),
          "MOV accepts PCM and H.264");
    CHECK(!resolve(MUX_MODE_MOV, "copy", MUX_VIDEO_UNKNOWN,
                   MUX_VIDEO_H264, &policy),
          "MOV rejects audio copy");
    CHECK(resolve(MUX_MODE_M4V, NULL, MUX_VIDEO_UNKNOWN,
                  MUX_VIDEO_HEVC, &policy) &&
          policy.audio_mode == MUX_AUDIO_AAC_AC3 &&
          policy.audio_mode_was_defaulted,
          "M4V defaults to AAC+AC3");
    CHECK(resolve(MUX_MODE_M4V, "fdk_aac_320", MUX_VIDEO_UNKNOWN,
                  MUX_VIDEO_H264, &policy),
          "M4V accepts AAC-only");
    CHECK(resolve(MUX_MODE_M4V, "aac", MUX_VIDEO_UNKNOWN,
                  MUX_VIDEO_H264, &policy) &&
          policy.audio_mode == MUX_AUDIO_AAC && !policy.audio_mode_was_defaulted,
          "M4V preserves explicit AAC alias");
    CHECK(!resolve(MUX_MODE_M4V, "copy", MUX_VIDEO_UNKNOWN,
                   MUX_VIDEO_H264, &policy), "M4V rejects audio copy");
    CHECK(!resolve(MUX_MODE_M4V, "pcm", MUX_VIDEO_UNKNOWN,
                   MUX_VIDEO_H264, &policy),
          "M4V rejects PCM");
    CHECK(!resolve(MUX_MODE_M4V, "fdk_aac_320", MUX_VIDEO_UNKNOWN,
                   MUX_VIDEO_PRORES, &policy),
          "M4V rejects ProRes replacement");
    CHECK(!resolve(MUX_MODE_MOV, "pcm", MUX_VIDEO_UNKNOWN,
                   MUX_VIDEO_UNSUPPORTED, &policy),
          "MOV rejects unsupported replacement codec");
    CHECK(!resolve(MUX_MODE_MKV, "unknown", MUX_VIDEO_UNKNOWN,
                   MUX_VIDEO_H264, &policy),
          "reject unknown audio mode");

    CHECK(mux_policy_resolve(&profile, MUX_MODE_MOV, "pcm",
                             MUX_VIDEO_UNKNOWN, MUX_VIDEO_H264,
                             &policy, error, sizeof(error)),
          "all policy resolutions succeed for valid tuples");
    CHECK(policy.preserve_chapters && policy.preserve_metadata &&
          policy.preserve_subtitles_when_supported && policy.preserve_video,
          "policy explicitly records preservation requirements");

    if (failures)
        return 1;
    puts("All mux policy tests passed.");
    return 0;
}
