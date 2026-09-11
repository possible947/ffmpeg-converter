#include "input_video_info.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void test_8bit_h264(void)
{
    InputVideoInfo info;
    const char *fixture =
        "codec_name=h264\n"
        "pix_fmt=yuv420p\n"
        "bits_per_raw_sample=8\n"
        "profile=High\n"
        "color_range=tv\n"
        "color_primaries=bt709\n"
        "color_transfer=bt709\n"
        "color_space=bt709\n"
        "width=1920\n"
        "height=1080\n"
        "avg_frame_rate=30000/1001\n";

    expect(input_video_info_parse_text(fixture, &info), "parse 8-bit fixture");
    expect(strcmp(info.codec_name, "h264") == 0, "parse codec name");
    expect(strcmp(info.pixel_format, "yuv420p") == 0, "parse pixel format");
    expect(strcmp(info.profile, "High") == 0, "parse profile");
    expect(info.bit_depth == 8, "detect 8-bit depth");
    expect(strcmp(info.color_range, "tv") == 0, "parse color range");
    expect(strcmp(info.color_primaries, "bt709") == 0, "parse color primaries");
    expect(strcmp(info.color_transfer, "bt709") == 0, "parse color transfer");
    expect(strcmp(info.color_space, "bt709") == 0, "parse color space");
    expect(info.width == 1920 && info.height == 1080, "parse dimensions");
    expect(info.fps > 29.96 && info.fps < 29.98, "parse rational frame rate");
}

static void test_10bit_p010(void)
{
    InputVideoInfo info;
    const char *fixture =
        "codec_name=hevc\n"
        "pix_fmt=p010le\n"
        "bits_per_raw_sample=N/A\n"
        "profile=Main 10\n"
        "color_range=tv\n"
        "color_primaries=bt709\n"
        "color_transfer=bt709\n"
        "color_space=bt709\n";

    expect(input_video_info_parse_text(fixture, &info), "parse p010 fixture");
    expect(strcmp(info.codec_name, "hevc") == 0, "parse HEVC codec");
    expect(info.bit_depth == 10, "detect p010 as 10-bit");
    expect(strcmp(info.color_primaries, "bt709") == 0, "parse 10-bit color metadata");
}

static void test_bits_per_raw_sample_fallback(void)
{
    InputVideoInfo info;
    const char *fixture =
        "codec_name=prores\n"
        "pix_fmt=N/A\n"
        "bits_per_raw_sample=10\n";

    expect(input_video_info_parse_text(fixture, &info), "parse bit-depth fallback fixture");
    expect(info.bit_depth == 10, "use bits_per_raw_sample when pixel format is unknown");
}

static void test_unknown_depth(void)
{
    InputVideoInfo info;
    const char *fixture = "codec_name=vp9\npix_fmt=N/A\nbits_per_raw_sample=N/A\n";

    expect(input_video_info_parse_text(fixture, &info), "parse unknown-depth fixture");
    expect(info.bit_depth == 0, "keep unknown bit depth as zero");
}

int main(int argc, char **argv)
{
    test_8bit_h264();
    test_10bit_p010();
    test_bits_per_raw_sample_fallback();
    test_unknown_depth();
    if (argc > 1) {
        InputVideoInfo info;
        expect(input_video_info_probe(argv[1], &info), "probe real input video");
        printf("real input: codec=%s pix_fmt=%s bit_depth=%d profile=%s\n",
               info.codec_name, info.pixel_format, info.bit_depth, info.profile);
    }
    if (failures != 0)
        return 1;
    puts("input video info tests passed");
    return 0;
}
