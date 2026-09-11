#ifndef FFMPEG_CONVERTER_INPUT_VIDEO_INFO_H
#define FFMPEG_CONVERTER_INPUT_VIDEO_INFO_H

#include <stddef.h>

typedef struct {
    char codec_name[64];
    char pixel_format[64];
    char profile[64];
    char color_range[32];
    char color_primaries[32];
    char color_transfer[32];
    char color_space[32];
    int bit_depth;
    int width;
    int height;
    double fps;
} InputVideoInfo;

void input_video_info_init(InputVideoInfo *info);
int input_video_info_parse_text(const char *text, InputVideoInfo *info);
int input_video_info_probe(const char *input_path, InputVideoInfo *info);

#endif
