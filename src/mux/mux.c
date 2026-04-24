#include "mux.h"
#include "mux_platform.h"
#include "converter_platform.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void emit_message(const ConverterCallbacks* callbacks, const char* text)
{
    if (callbacks && callbacks->on_message)
        callbacks->on_message(text);
}

static void emit_error(const ConverterCallbacks* callbacks, const char* text, ConverterError code)
{
    if (callbacks && callbacks->on_error)
        callbacks->on_error(text, code);
}

static void emit_stage(const ConverterCallbacks* callbacks, const char* text)
{
    if (callbacks && callbacks->on_stage)
        callbacks->on_stage(text);
}

static int has_suffix_ci(const char* text, const char* suffix)
{
    size_t text_len;
    size_t suffix_len;

    if (!text || !suffix)
        return 0;

    text_len = strlen(text);
    suffix_len = strlen(suffix);
    if (text_len < suffix_len)
        return 0;

    text += text_len - suffix_len;
    while (*text && *suffix) {
        if (tolower((unsigned char)*text) != tolower((unsigned char)*suffix))
            return 0;
        ++text;
        ++suffix;
    }

    return *suffix == '\0';
}

static int video_track_needs_forced_fps(const char* path)
{
    return has_suffix_ci(path, ".hevc") ||
           has_suffix_ci(path, ".h265") ||
           has_suffix_ci(path, ".264") ||
           has_suffix_ci(path, ".h264");
}

static double parse_rate_to_fps(const char* text)
{
    double numerator = 0.0;
    double denominator = 1.0;

    if (!text || text[0] == '\0')
        return 0.0;

    if (sscanf(text, "%lf/%lf", &numerator, &denominator) == 2) {
        if (denominator == 0.0)
            return 0.0;
        return numerator / denominator;
    }

    return atof(text);
}

static int probe_video_rate_string(const char* ffprobe_bin,
                                   const char* input_file,
                                   char* rate_out,
                                   size_t rate_out_sz)
{
    char cmd[8192];
    char quoted_tool[2048];
    char quoted_input[2048];
    FILE* fp;

    if (!ffprobe_bin || !input_file || !rate_out || rate_out_sz == 0)
        return 0;

    platform_shell_quote(ffprobe_bin, quoted_tool, sizeof(quoted_tool));
    platform_shell_quote(input_file, quoted_input, sizeof(quoted_input));

    snprintf(cmd,
             sizeof(cmd),
             "%s -v error -select_streams v:0 -show_entries stream=avg_frame_rate "
             "-of default=noprint_wrappers=1:nokey=1 %s %s",
             quoted_tool,
             quoted_input,
             platform_null_redirect());
    fp = mux_platform_popen(cmd, "r");
    if (!fp)
        return 0;

    if (!fgets(rate_out, (int)rate_out_sz, fp)) {
        platform_pclose_exitcode(fp);
        return 0;
    }
    platform_pclose_exitcode(fp);
    rate_out[strcspn(rate_out, "\r\n")] = '\0';

    if (rate_out[0] != '\0' && strcmp(rate_out, "0/0") != 0)
        return 1;

    snprintf(cmd,
             sizeof(cmd),
             "%s -v error -select_streams v:0 -show_entries stream=r_frame_rate "
             "-of default=noprint_wrappers=1:nokey=1 %s %s",
             quoted_tool,
             quoted_input,
             platform_null_redirect());
    fp = mux_platform_popen(cmd, "r");
    if (!fp)
        return 0;

    rate_out[0] = '\0';
    if (!fgets(rate_out, (int)rate_out_sz, fp)) {
        platform_pclose_exitcode(fp);
        return 0;
    }
    platform_pclose_exitcode(fp);
    rate_out[strcspn(rate_out, "\r\n")] = '\0';

    return rate_out[0] != '\0' && strcmp(rate_out, "0/0") != 0;
}

static int validate_mux_output(const char* ffprobe_bin, const char* output_file)
{
    char cmd[8192];
    char quoted_tool[2048];
    char quoted_output[2048];
    FILE* fp;
    char line[128];
    int has_audio = 0;
    int has_video = 0;

    if (!platform_file_is_regular(output_file))
        return 0;

    platform_shell_quote(ffprobe_bin, quoted_tool, sizeof(quoted_tool));
    platform_shell_quote(output_file, quoted_output, sizeof(quoted_output));

    snprintf(cmd,
             sizeof(cmd),
             "%s -v error -show_entries stream=codec_type "
             "-of default=noprint_wrappers=1:nokey=1 %s %s",
             quoted_tool,
             quoted_output,
             platform_null_redirect());
    fp = mux_platform_popen(cmd, "r");
    if (!fp)
        return 0;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "video") == 0)
            has_video = 1;
        else if (strcmp(line, "audio") == 0)
            has_audio = 1;
    }

    platform_pclose_exitcode(fp);
    return has_video && has_audio;
}

static int run_mux_command(const char* cmd, const ConverterCallbacks* callbacks)
{
    FILE* fp;
    char line[1024];

    fp = mux_platform_popen(cmd, "r");
    if (!fp)
        return -1;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0')
            emit_message(callbacks, line);
    }

    return platform_pclose_exitcode(fp);
}

ConverterError mux_run_postprocess(
    const MuxOptions* opts,
    const ConvertOptions* convert_opts,
    const ConverterCallbacks* callbacks
) {
    const char* ffprobe_bin;
    const char* mkvmerge_bin;
    char rate[64];
    char cmd[12288];
    char quoted_tool[2048];
    char quoted_output[2048];
    char quoted_track[2048];
    char quoted_intermediate[2048];
    char temp_output[1200];
    char timing_arg[128];

    (void)convert_opts;

    if (!opts)
        return ERR_INVALID_OPTIONS;

    if (!platform_file_is_regular(opts->intermediate_file)) {
        emit_error(callbacks, "post-mux failed: intermediate file not found", ERR_INPUT_NOT_FOUND);
        return ERR_INPUT_NOT_FOUND;
    }

    if (!platform_file_is_regular(opts->video_track_file)) {
        emit_error(callbacks, "post-mux failed: video-track file not found", ERR_INPUT_NOT_FOUND);
        return ERR_INPUT_NOT_FOUND;
    }

    ffprobe_bin = platform_get_ffprobe_bin();
    mkvmerge_bin = platform_get_mkvmerge_bin();
    if (!mkvmerge_bin || mkvmerge_bin[0] == '\0') {
        emit_error(callbacks, "post-mux failed: mkvmerge not found", ERR_INVALID_OPTIONS);
        return ERR_INVALID_OPTIONS;
    }

    snprintf(temp_output, sizeof(temp_output), "%s.postmux.tmp.mkv", opts->output_file);
    platform_unlink(temp_output);

    timing_arg[0] = '\0';
    if (video_track_needs_forced_fps(opts->video_track_file)) {
        if (!probe_video_rate_string(ffprobe_bin, opts->intermediate_file, rate, sizeof(rate))) {
            emit_error(callbacks, "post-mux failed: could not probe source FPS", ERR_FFPROBE_FAILED);
            return ERR_FFPROBE_FAILED;
        }
        snprintf(timing_arg, sizeof(timing_arg), "--default-duration 0:%sfps ", rate);
    }

    platform_shell_quote(mkvmerge_bin, quoted_tool, sizeof(quoted_tool));
    platform_shell_quote(temp_output, quoted_output, sizeof(quoted_output));
    platform_shell_quote(opts->video_track_file, quoted_track, sizeof(quoted_track));
    platform_shell_quote(opts->intermediate_file, quoted_intermediate, sizeof(quoted_intermediate));

    snprintf(cmd,
             sizeof(cmd),
             "%s -o %s --no-audio --no-subtitles --no-buttons --no-attachments "
             "--no-chapters --no-global-tags --no-track-tags %s%s --no-video %s 2>&1",
             quoted_tool,
             quoted_output,
             timing_arg,
             quoted_track,
             quoted_intermediate);

    emit_stage(callbacks, "Post-mux (mkvmerge)");
    emit_message(callbacks, cmd);

    if (run_mux_command(cmd, callbacks) != 0) {
        platform_unlink(temp_output);
        emit_error(callbacks, "post-mux failed: mkvmerge returned error", ERR_FFMPEG_FAILED);
        return ERR_FFMPEG_FAILED;
    }

    if (!validate_mux_output(ffprobe_bin, temp_output)) {
        platform_unlink(temp_output);
        emit_error(callbacks, "post-mux failed: output validation failed", ERR_FFPROBE_FAILED);
        return ERR_FFPROBE_FAILED;
    }

    if (rename(temp_output, opts->output_file) != 0) {
        platform_unlink(temp_output);
        emit_error(callbacks, "post-mux failed: could not move validated output into place", ERR_UNKNOWN);
        return ERR_UNKNOWN;
    }

    emit_message(callbacks, "Post-mux completed");
    return ERR_OK;
}
