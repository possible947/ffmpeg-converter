#include "converter.h"
#include "converter_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FFMPEG_CONVERTER_SOURCE_DIR
#define FFMPEG_CONVERTER_SOURCE_DIR "."
#endif

static int failures;

static void expect_string(const char *actual, const char *expected, const char *message)
{
    if (!actual || strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s\n", message);
        fprintf(stderr, "  expected: %s\n", expected);
        fprintf(stderr, "  actual:   %s\n", actual ? actual : "(null)");
        failures++;
    }
}

static void test_hardware_args_from_presets_json(void)
{
    ConvertOptions opts;
    memset(&opts, 0, sizeof(opts));

    snprintf(opts.codec, sizeof(opts.codec), "%s", "av1_vaapi");
    snprintf(opts.preset, sizeof(opts.preset), "%s", "quality");
    expect_string(
        platform_get_video_codec_flags(opts.codec, NULL, &opts),
        "-c:v av1_vaapi -rc_mode CQP -global_quality 22 ",
        "av1_vaapi/quality uses presets.json ffmpeg_args");

    snprintf(opts.codec, sizeof(opts.codec), "%s", "av1_vaapi_10bit");
    snprintf(opts.preset, sizeof(opts.preset), "%s", "quality");
    expect_string(
        platform_get_video_codec_flags(opts.codec, NULL, &opts),
        "-c:v av1_vaapi -rc_mode CQP -global_quality 22 ",
        "av1_vaapi_10bit/quality uses av1_vaapi presets.json ffmpeg_args");

    snprintf(opts.codec, sizeof(opts.codec), "%s", "hevc_nvenc_10bit");
    snprintf(opts.preset, sizeof(opts.preset), "%s", "balance");
    expect_string(
        platform_get_video_codec_flags(opts.codec, NULL, &opts),
        "-c:v hevc_nvenc -preset p4 -cq 25 -lookahead_level auto ",
        "hevc_nvenc_10bit/balance uses hevc_nvenc presets.json ffmpeg_args");

    snprintf(opts.codec, sizeof(opts.codec), "%s", "h264_qsv");
    snprintf(opts.preset, sizeof(opts.preset), "%s", "speed");
    expect_string(
        platform_get_video_codec_flags(opts.codec, NULL, &opts),
        "-c:v h264_qsv -global_quality 22 -preset veryfast -extbrc 1 ",
        "h264_qsv/speed uses presets.json ffmpeg_args");

    snprintf(opts.codec, sizeof(opts.codec), "%s", "hevc_qsv");
    snprintf(opts.preset, sizeof(opts.preset), "%s", "balance");
    expect_string(
        platform_get_video_codec_flags(opts.codec, NULL, &opts),
        "-c:v hevc_qsv -global_quality 25 -preset medium -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1 ",
        "hevc_qsv/balance uses presets.json ffmpeg_args");

    snprintf(opts.codec, sizeof(opts.codec), "%s", "hevc_qsv_10bit");
    snprintf(opts.preset, sizeof(opts.preset), "%s", "balance");
    expect_string(
        platform_get_video_codec_flags(opts.codec, NULL, &opts),
        "-c:v hevc_qsv -global_quality 25 -preset medium -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1 ",
        "hevc_qsv_10bit/balance uses hevc_qsv presets.json ffmpeg_args");

    snprintf(opts.codec, sizeof(opts.codec), "%s", "av1_qsv");
    snprintf(opts.preset, sizeof(opts.preset), "%s", "quality");
    expect_string(
        platform_get_video_codec_flags(opts.codec, NULL, &opts),
        "-c:v av1_qsv -global_quality 28 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1 ",
        "av1_qsv/quality uses presets.json ffmpeg_args");

    snprintf(opts.codec, sizeof(opts.codec), "%s", "av1_qsv_10bit");
    snprintf(opts.preset, sizeof(opts.preset), "%s", "quality");
    expect_string(
        platform_get_video_codec_flags(opts.codec, NULL, &opts),
        "-c:v av1_qsv -global_quality 28 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1 ",
        "av1_qsv_10bit/quality uses av1_qsv presets.json ffmpeg_args");
}

int main(void)
{
    setenv("PRESETS_PATH", FFMPEG_CONVERTER_SOURCE_DIR, 1);

    test_hardware_args_from_presets_json();
    platform_cleanup();

    if (failures != 0)
        return 1;

    puts("linux preset args tests passed");
    return 0;
}
