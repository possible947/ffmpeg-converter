#include <stdio.h>
#include <string.h>

#include "cli_common.h"
#include "converter_platform.h"
#include "mux_policy.h"
#include "progress.h"

static int failures, audio_checks;
static char checked_audio[32];
static const char *unavailable_audio, *unavailable_codec;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

/* Only runtime availability, filesystem access and progress output are mocked.
 * Selection, presets, parsing, policy and video flags use production code. */
int platform_codec_is_available(const CliPlatformHandle *h, const char *codec)
{
    (void)h;
    return codec && (!unavailable_codec || strcmp(codec, unavailable_codec));
}
int platform_audio_mode_is_available(const char *mode)
{
    audio_checks++;
    snprintf(checked_audio, sizeof(checked_audio), "%s", mode ? mode : "");
    return mode && (!unavailable_audio || strcmp(mode, unavailable_audio)) &&
           (!strcmp(mode, "copy") || !strcmp(mode, "pcm") ||
            !strcmp(mode, "fdk_aac_320") || !strcmp(mode, "fdk_aac_320_ac3_640"));
}
int platform_mux_is_supported(void) { return 1; }
int platform_m4v_is_supported(void) { return 1; }
int platform_hq_is_available(void) { return 0; }
int platform_get_default_vulkan_device(const CliPlatformHandle *h, const char *codec)
{
    (void)h;
    (void)codec;
    return 1;
}
int platform_file_is_regular_readable(const char *path) { return path != NULL; }
int platform_ensure_output_dir(const char *path) { return path != NULL; }
int cli_get_presets_v2_path(char *path, size_t size)
{
    return snprintf(path, size, "%s", CLI_TEST_PRESETS_PATH) < (int)size;
}
void progress_end(void) {}
void progress_update(double percent, double fps, double eta)
{
    (void)percent;
    (void)fps;
    (void)eta;
}

static int parse(int argc, char **argv, ConvertOptions *opts, CliM4VOptions *m4v)
{
    const char *files[32] = {0};
    int count = -1, result;
    audio_checks = 0;
    checked_audio[0] = '\0';
    result = parse_args(argc, argv, NULL, opts, m4v, files, &count);
    if (result) {
        CHECK(count == 1);
        CHECK(files[0] && !strcmp(files[0], "input.mov"));
    }
    return result;
}

static void test_m4v_audio(void)
{
    static const struct {
        const char *requested, *canonical;
        int accepted, audio_mode;
    } cases[] = {
        {NULL, "fdk_aac_320_ac3_640", 1, 0},
        {"aac", "fdk_aac_320", 1, 1},
        {"fdk_aac_320", "fdk_aac_320", 1, 1},
        {"aac+ac3", "fdk_aac_320_ac3_640", 1, 0},
        {"fdk_aac_320_ac3_640", "fdk_aac_320_ac3_640", 1, 0},
        {"pcm", "pcm", 0, 0}, {"copy", "copy", 0, 0}
    };
    MuxBuildProfile profile;
    MuxPolicy policy;
    char error[256];
    size_t i;
    CHECK(mux_build_profile_default(&profile));
    CHECK(mux_policy_resolve(&profile, MUX_MODE_M4V, NULL,
                            MUX_VIDEO_UNKNOWN, MUX_VIDEO_UNKNOWN,
                            &policy, error, sizeof(error)));
    CHECK(policy.audio_mode_was_defaulted);
    CHECK(policy.audio_mode == MUX_AUDIO_AAC_AC3);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ConvertOptions opts;
        CliM4VOptions m4v;
        char *argv[16] = {"cli", "--codec", "mux", "--encoder", "m4v"};
        int argc = 5;
        if (cases[i].requested) {
            argv[argc++] = "--audio-output";
            argv[argc++] = (char *)cases[i].requested;
        }
        argv[argc++] = "input.mov";
        CHECK(parse(argc, argv, &opts, &m4v) == cases[i].accepted);
        CHECK(audio_checks == (cases[i].requested ? 1 : 0));
        if (cases[i].requested)
            CHECK(!strcmp(checked_audio, cases[i].canonical));
        if (!cases[i].accepted)
            continue;
        CHECK(!strcmp(opts.codec, "mux"));
        CHECK(!strcmp(opts.preset, "m4v"));
        CHECK(!strcmp(opts.audio_output_mode, cases[i].canonical));
        CHECK(m4v.audio_mode == cases[i].audio_mode);
        if (!cases[i].requested)
            CHECK(!strcmp(opts.audio_output_mode, mux_audio_mode_name(policy.audio_mode)));
    }
    {
        ConvertOptions opts;
        CliM4VOptions m4v;
        /* Explicit preset, not raw encoder selection, determines the container. */
        char *argv[] = {"cli", "--codec", "mux", "--encoder", "mkv",
                        "--preset", "m4v", "input.mov"};
        CHECK(parse(8, argv, &opts, &m4v));
        CHECK(!strcmp(opts.audio_output_mode, "fdk_aac_320_ac3_640"));
        CHECK(m4v.audio_mode == 0);
    }
    {
        ConvertOptions opts;
        char *argv[] = {"cli", "--codec", "mux", "--encoder", "m4v", "input.mov"};
        CHECK(parse(6, argv, &opts, NULL));
        CHECK(!strcmp(opts.audio_output_mode, "fdk_aac_320_ac3_640"));
    }
}

static void test_container_defaults(void)
{
    const char *containers[] = {"mkv", "mov"};
    size_t i;
    for (i = 0; i < sizeof(containers) / sizeof(containers[0]); i++) {
        ConvertOptions opts;
        CliM4VOptions m4v;
        char *argv[] = {"cli", "--codec", "mux", "--encoder",
                        (char *)containers[i], "input.mov"};
        CHECK(parse(6, argv, &opts, &m4v));
        CHECK(!strcmp(opts.codec, "mux"));
        CHECK(!strcmp(opts.preset, containers[i]));
        CHECK(!strcmp(opts.audio_output_mode, "pcm"));
        CHECK(audio_checks == 0);
    }
    {
        ConvertOptions opts;
        CliM4VOptions m4v;
        char *argv[] = {"cli", "--codec", "mux", "--encoder", "m4v",
                        "--preset", "mov", "input.mov"};
        CHECK(parse(8, argv, &opts, &m4v));
        CHECK(!strcmp(opts.preset, "mov"));
        CHECK(!strcmp(opts.audio_output_mode, "pcm"));
    }
}

static void test_audio_video_isolation(void)
{
    static const struct {
        const char *group, *encoder, *codec, *preset, *deblock;
    } selections[] = {
        {"software", "prores", "prores", "hq", "weak"},
        {"software", "prores_ks", "prores_ks", "4444", "strong"},
        {"vaapi", "hevc_10bit", "hevc_vaapi_10bit", "quality", NULL},
        {"nvenc", "h264", "h264_nvenc", "speed", NULL},
        {"qsv", "hevc", "hevc_qsv", "balance", NULL},
        {"amf", "h264", "h264_amf", "quality", NULL},
        {"vulkan", "prores_ks", "prores_ks_vulkan", "hq", NULL},
        {"vulkan", "hevc", "hevc_vulkan", "quality", NULL}
    };
    static const struct {
        const char *requested, *canonical;
    } audio[] = {
        {"pcm", "pcm"}, {"aac", "fdk_aac_320"}, {"fdk_aac_320", "fdk_aac_320"},
        {"aac+ac3", "fdk_aac_320_ac3_640"},
        {"fdk_aac_320_ac3_640", "fdk_aac_320_ac3_640"}
    };
    size_t i, j;
    for (i = 0; i < sizeof(selections) / sizeof(selections[0]); i++) {
        ConvertOptions baseline;
        char baseline_flags[1024] = "";
        for (j = 0; j < sizeof(audio) / sizeof(audio[0]); j++) {
            ConvertOptions opts;
            CliM4VOptions m4v;
            const char *flags;
            char *argv[32] = {"cli", "--codec", (char *)selections[i].group,
                             "--encoder", (char *)selections[i].encoder,
                             "--preset", (char *)selections[i].preset,
                             "--audio-norm", "loudnorm2", "--genre", "rock",
                             "--hw_device", "/dev/dri/renderD129",
                             "--vk_device", "2", "--overwrite", "--dry-run"};
            int argc = 17;
            if (selections[i].deblock) {
                argv[argc++] = "--deblock";
                argv[argc++] = (char *)selections[i].deblock;
            }
            argv[argc++] = "--audio-output";
            argv[argc++] = (char *)audio[j].requested;
            argv[argc++] = "input.mov";
            CHECK(parse(argc, argv, &opts, &m4v));
            CHECK(audio_checks == 1);
            CHECK(!strcmp(checked_audio, audio[j].canonical));
            CHECK(!strcmp(opts.audio_output_mode, audio[j].canonical));
            CHECK(!strcmp(opts.codec, selections[i].codec));
            CHECK(!strcmp(opts.preset, selections[i].preset));
            CHECK(opts.deblock == (selections[i].deblock ?
                  (!strcmp(selections[i].deblock, "weak") ? 2 : 3) : 1));
            CHECK(opts.video_quality == 0);
            CHECK(opts.vulkan_device == 2);
            CHECK(!strcmp(opts.hw_device, "/dev/dri/renderD129"));
            CHECK(!strcmp(opts.audio_norm, "loudness_norm_2pass"));
            CHECK(opts.genre == 2 && opts.overwrite && opts.dry_run);
            CHECK(m4v.audio_mode == 0);
            flags = platform_get_video_codec_flags(opts.codec, NULL, &opts);
            if (!strcmp(selections[i].group, "software"))
                CHECK(flags == NULL);
            else
                CHECK(flags && flags[0]);
            if (flags) {
                if (j == 0)
                    snprintf(baseline_flags, sizeof(baseline_flags), "%s", flags);
                else
                    CHECK(!strcmp(baseline_flags, flags));
            }
            /* parse_args zeroes padding: compare every option except audio. */
            memset(opts.audio_output_mode, 0, sizeof(opts.audio_output_mode));
            if (j == 0)
                memcpy(&baseline, &opts, sizeof(baseline));
            else
                CHECK(!memcmp(&baseline, &opts, sizeof(opts)));
        }
    }
}

static void test_validation_preserved(void)
{
    ConvertOptions opts;
    CliM4VOptions m4v;
    char *alias[] = {"cli", "--codec", "mux", "--encoder", "m4v",
                     "--audio-output", "aac", "input.mov"};
    char *invalid[] = {"cli", "--audio-output", "unknown", "input.mov"};
    char *missing[] = {"cli", "--audio-output"};
    const char *audio[] = {"pcm", "aac", "aac+ac3"};
    size_t i;
    unavailable_audio = "fdk_aac_320";
    CHECK(!parse(8, alias, &opts, &m4v));
    CHECK(audio_checks == 1 && !strcmp(checked_audio, "fdk_aac_320"));
    unavailable_audio = NULL;
    CHECK(!parse(4, invalid, &opts, &m4v));
    CHECK(audio_checks == 0);
    CHECK(!parse(2, missing, &opts, &m4v));
    CHECK(audio_checks == 0);
    for (i = 0; i < sizeof(audio) / sizeof(audio[0]); i++) {
        char *hw[] = {"cli", "--codec", "vaapi", "--encoder", "h264",
                      "--deblock", "weak", "--audio-output", (char *)audio[i], "input.mov"};
        char *preset[] = {"cli", "--codec", "software", "--encoder", "prores_ks",
                          "--preset", "invalid", "--audio-output", (char *)audio[i], "input.mov"};
        CHECK(!parse(10, hw, &opts, &m4v));
        CHECK(!parse(10, preset, &opts, &m4v));
    }
    unavailable_codec = "mux";
    CHECK(!parse(8, alias, &opts, &m4v));
    unavailable_codec = NULL;
}

int main(void)
{
    test_m4v_audio();
    test_container_defaults();
#ifndef FFMPEG_CONVERTER_CLI_PORTABLE_POLICY_ONLY
    test_audio_video_isolation();
#endif
    test_validation_preserved();
    if (failures) {
        fprintf(stderr, "%d CLI policy assertion(s) failed\n", failures);
        return 1;
    }
    puts("CLI M4V policy and audio/video isolation regressions passed");
    return 0;
}
