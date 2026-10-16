#include "mux_policy.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void test_profile(const MuxBuildProfile *profile)
{
    AudioModeAvailability available = audio_mode_availability(
        profile, AUDIO_WORKFLOW_SOFTWARE, MUX_MODE_INVALID, NULL, 0,
        MUX_VIDEO_UNKNOWN);
    CHECK(available.count == 3);
    CHECK(available.modes[0] == MUX_AUDIO_PCM);
    CHECK(audio_mode_availability_contains(&available, MUX_AUDIO_PCM));
    CHECK(audio_mode_availability_contains(&available, MUX_AUDIO_AAC));
    CHECK(audio_mode_availability_contains(&available, MUX_AUDIO_AAC_AC3));
    CHECK(!audio_mode_availability_contains(&available, MUX_AUDIO_COPY));
    CHECK(!audio_mode_availability_contains(&available, MUX_AUDIO_INVALID));

    available = audio_mode_availability(
        profile, AUDIO_WORKFLOW_MUX, MUX_MODE_M4V, NULL, 0, MUX_VIDEO_UNKNOWN);
    CHECK(available.count == 2);
    CHECK(available.modes[0] == MUX_AUDIO_AAC_AC3);
    CHECK(audio_mode_availability_contains(&available, MUX_AUDIO_AAC));
    CHECK(audio_mode_availability_contains(&available, MUX_AUDIO_AAC_AC3));
    CHECK(!audio_mode_availability_contains(&available, MUX_AUDIO_PCM));
    CHECK(!audio_mode_availability_contains(&available, MUX_AUDIO_COPY));

    for (MuxVideoCodec video = MUX_VIDEO_PRORES; video <= MUX_VIDEO_AV1; video++) {
        MuxPolicy policy;
        available = audio_mode_availability(
            profile, AUDIO_WORKFLOW_HQ, MUX_MODE_INVALID, NULL, 0, video);
        CHECK(mux_policy_resolve_hq(profile, video, NULL, &policy, NULL, 0));
        CHECK(available.count > 0 && available.modes[0] == policy.audio_mode);
        if (video == MUX_VIDEO_PRORES) {
            CHECK(available.count == 1);
            CHECK(available.modes[0] == MUX_AUDIO_PCM);
        } else {
            MuxAudioMode expected[4];
            size_t count = mux_policy_list_audio_modes(
                profile, MUX_MODE_MKV, NULL, 0, video, expected, 4);
            CHECK(available.count == count);
            for (size_t i = 0; i < count; i++)
                CHECK(available.modes[i] == expected[i]);
        }
    }

    for (MuxMode mode = MUX_MODE_COPY; mode < MUX_MODE_INVALID; mode++) {
        MuxVideoCodec sources[] = {MUX_VIDEO_H264, MUX_VIDEO_PRORES};
        MuxAudioMode expected[4];
        size_t count = mux_policy_list_audio_modes(
            profile, mode, sources, 2, MUX_VIDEO_UNKNOWN, expected, 4);
        available = audio_mode_availability(
            profile, AUDIO_WORKFLOW_MUX, mode, sources, 2, MUX_VIDEO_UNKNOWN);
        CHECK(available.count == count);
        for (size_t i = 0; i < count; i++)
            CHECK(available.modes[i] == expected[i]);
        if (mode == MUX_MODE_COPY)
            CHECK(available.count == 1 && available.modes[0] == MUX_AUDIO_PCM);
    }
    available = audio_mode_availability(
        profile, AUDIO_WORKFLOW_HQ, MUX_MODE_INVALID, NULL, 0, MUX_VIDEO_UNKNOWN);
    CHECK(available.count == 0);
}

int main(void)
{
    for (MuxPlatform platform = MUX_PLATFORM_LINUX;
         platform <= MUX_PLATFORM_MACOS; platform++) {
        for (MuxFfmpegVersion version = MUX_FFMPEG_8_1;
             version <= MUX_FFMPEG_9_0; version++) {
            MuxBuildProfile profile = {platform, version};
            test_profile(&profile);
        }
    }
    puts("Context-aware audio availability tests passed");
    return 0;
}
