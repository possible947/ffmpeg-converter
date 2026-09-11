#include "input_video_info.h"
#include "converter_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define NEXT_TOKEN(value, delimiters, context) strtok_s((value), (delimiters), (context))
#else
#define NEXT_TOKEN(value, delimiters, context) strtok_r((value), (delimiters), (context))
#endif

static void copy_field(char *dst, size_t dst_size, const char *value)
{
    if (!dst || dst_size == 0)
        return;
    if (!value)
        value = "";
    strncpy(dst, value, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static int parse_fps(const char *value, double *fps)
{
    double numerator;
    double denominator;

    if (!value || !fps || value[0] == '\0' || strcmp(value, "N/A") == 0)
        return 0;
    if (sscanf(value, "%lf/%lf", &numerator, &denominator) == 2) {
        if (denominator == 0.0)
            return 0;
        *fps = numerator / denominator;
        return 1;
    }
    *fps = strtod(value, NULL);
    return *fps > 0.0;
}

static int bit_depth_from_pixel_format(const char *pixel_format)
{
    const char *format;

    if (!pixel_format || pixel_format[0] == '\0' ||
        strcmp(pixel_format, "N/A") == 0)
        return 0;
    format = pixel_format;
    if (strstr(format, "12le") || strstr(format, "12be") || strstr(format, "p012"))
        return 12;
    if (strstr(format, "10le") || strstr(format, "10be") ||
        strstr(format, "p010") || strstr(format, "x2rgb10"))
        return 10;
    if (strstr(format, "14le") || strstr(format, "14be"))
        return 14;
    if (strstr(format, "16le") || strstr(format, "16be") || strstr(format, "p016"))
        return 16;
    if (strstr(format, "9le") || strstr(format, "9be"))
        return 9;
    return 8;
}

static void update_bit_depth(InputVideoInfo *info, const char *value)
{
    char *end;
    long parsed;

    if (!info)
        return;
    if (info->pixel_format[0] != '\0') {
        int pixel_depth = bit_depth_from_pixel_format(info->pixel_format);
        if (pixel_depth > 0) {
            info->bit_depth = pixel_depth;
            return;
        }
    }
    if (!value || value[0] == '\0' || strcmp(value, "N/A") == 0)
        return;
    parsed = strtol(value, &end, 10);
    if (*end == '\0' && parsed > 0 && parsed <= 64)
        info->bit_depth = (int)parsed;
}

void input_video_info_init(InputVideoInfo *info)
{
    if (!info)
        return;
    memset(info, 0, sizeof(*info));
}

int input_video_info_parse_text(const char *text, InputVideoInfo *info)
{
    char *copy;
    char *line;
    char *saveptr = NULL;
    int found = 0;

    if (!text || !info)
        return 0;
    input_video_info_init(info);
    copy = malloc(strlen(text) + 1);
    if (!copy)
        return 0;
    strcpy(copy, text);

    line = NEXT_TOKEN(copy, "\n", &saveptr);
    while (line) {
        char *separator = strchr(line, '=');
        char *key = line;
        char *value;
        size_t length;

        while (*key == ' ' || *key == '\t' || *key == '\r')
            key++;
        if (separator) {
            *separator = '\0';
            value = separator + 1;
            length = strlen(value);
            while (length > 0 && (value[length - 1] == '\r' || value[length - 1] == ' ' || value[length - 1] == '\t'))
                value[--length] = '\0';
            if (strcmp(key, "codec_name") == 0)
                copy_field(info->codec_name, sizeof(info->codec_name), value);
            else if (strcmp(key, "pix_fmt") == 0)
                copy_field(info->pixel_format, sizeof(info->pixel_format), value);
            else if (strcmp(key, "profile") == 0)
                copy_field(info->profile, sizeof(info->profile), value);
            else if (strcmp(key, "color_range") == 0)
                copy_field(info->color_range, sizeof(info->color_range), value);
            else if (strcmp(key, "color_primaries") == 0)
                copy_field(info->color_primaries, sizeof(info->color_primaries), value);
            else if (strcmp(key, "color_transfer") == 0)
                copy_field(info->color_transfer, sizeof(info->color_transfer), value);
            else if (strcmp(key, "color_space") == 0)
                copy_field(info->color_space, sizeof(info->color_space), value);
            else if (strcmp(key, "bits_per_raw_sample") == 0)
                update_bit_depth(info, value);
            else if (strcmp(key, "width") == 0)
                info->width = atoi(value);
            else if (strcmp(key, "height") == 0)
                info->height = atoi(value);
            else if (strcmp(key, "avg_frame_rate") == 0)
                parse_fps(value, &info->fps);
        }
        line = NEXT_TOKEN(NULL, "\n", &saveptr);
    }
    free(copy);

    if (info->pixel_format[0] != '\0') {
        int pixel_depth = bit_depth_from_pixel_format(info->pixel_format);
        if (pixel_depth > 0)
            info->bit_depth = pixel_depth;
    }
    found = info->codec_name[0] != '\0' || info->pixel_format[0] != '\0';
    return found;
}

int input_video_info_probe(const char *input_path, InputVideoInfo *info)
{
    const char *ffprobe_bin;
    char *escaped_tool;
    char *escaped_input;
    char command[8192];
    char output[4096];
    FILE *pipe;
    size_t used = 0;
    int status;

    if (!input_path || !info)
        return 0;
    ffprobe_bin = platform_get_ffprobe_bin();
    if (!ffprobe_bin || ffprobe_bin[0] == '\0')
        return 0;
    escaped_tool = platform_escape_path_for_command(ffprobe_bin);
    escaped_input = platform_escape_path_for_command(input_path);
    if (!escaped_tool || !escaped_input) {
        free(escaped_tool);
        free(escaped_input);
        return 0;
    }
    snprintf(command, sizeof(command),
             "%s -v error -select_streams v:0 "
             "-show_entries stream=codec_name,pix_fmt,bits_per_raw_sample,profile,width,height,avg_frame_rate,color_range,color_primaries,color_transfer,color_space "
             "-of default=noprint_wrappers=1 %s 2>%s",
             escaped_tool, escaped_input, platform_get_null_device());
    free(escaped_tool);
    free(escaped_input);

    pipe = platform_popen(command, "r");
    if (!pipe)
        return 0;
    output[0] = '\0';
    while (used + 1 < sizeof(output) && fgets(output + used, sizeof(output) - used, pipe))
        used = strlen(output);
    status = platform_pclose(pipe);
    if (status != 0)
        return 0;
    return input_video_info_parse_text(output, info);
}
