#include "mux.h"
#include "mux_platform.h"
#include "converter_platform.h"
#include "m4v.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

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

/* Reads the original source's video-track language tag from intermediate_file
 * (the ffmpeg-copied source), since opts->video_track_file is typically a raw
 * elementary stream with no container metadata of its own. */
static int probe_video_track_language(const char* ffprobe_bin,
                                      const char* input_file,
                                      char* lang_out,
                                      size_t lang_out_sz)
{
    char cmd[8192];
    char quoted_tool[2048];
    char quoted_input[2048];
    FILE* fp;

    if (!ffprobe_bin || !input_file || !lang_out || lang_out_sz == 0)
        return 0;

    platform_shell_quote(ffprobe_bin, quoted_tool, sizeof(quoted_tool));
    platform_shell_quote(input_file, quoted_input, sizeof(quoted_input));

    snprintf(cmd,
             sizeof(cmd),
             "%s -v error -select_streams v:0 -show_entries stream_tags=language "
             "-of default=noprint_wrappers=1:nokey=1 %s %s",
             quoted_tool,
             quoted_input,
             platform_null_redirect());
    fp = mux_platform_popen(cmd, "r");
    if (!fp)
        return 0;

    if (!fgets(lang_out, (int)lang_out_sz, fp)) {
        platform_pclose_exitcode(fp);
        return 0;
    }
    platform_pclose_exitcode(fp);
    lang_out[strcspn(lang_out, "\r\n")] = '\0';

    return lang_out[0] != '\0';
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

static unsigned long current_process_id(void)
{
#if defined(_WIN32)
    return (unsigned long)_getpid();
#else
    return (unsigned long)getpid();
#endif
}

static int build_unique_temp_output_path(const char* output_file,
                                         char* temp_output,
                                         size_t temp_output_sz)
{
    static int seeded = 0;
    unsigned long pid;
    int i;

    if (!output_file || !temp_output || temp_output_sz == 0)
        return 0;

    pid = current_process_id();
    if (!seeded) {
        unsigned int seed = (unsigned int)time(NULL) ^
                            (unsigned int)pid ^
                            (unsigned int)(uintptr_t)temp_output;
        srand(seed);
        seeded = 1;
    }

    for (i = 0; i < 32; ++i) {
        unsigned int token = (unsigned int)rand();
        int written = snprintf(temp_output,
                               temp_output_sz,
                               "%s.postmux.%lu.%u.%d.tmp.mkv",
                               output_file,
                               pid,
                               token,
                               i);
        if (written <= 0 || (size_t)written >= temp_output_sz)
            continue;
        if (strcmp(temp_output, output_file) == 0)
            continue;
        if (!platform_file_is_regular(temp_output))
            return 1;
    }

    return 0;
}

ConverterError mux_run_postprocess(
    const MuxOptions* opts,
    const ConvertOptions* convert_opts,
    const ConverterCallbacks* callbacks
) {
    const char* ffprobe_bin;
    const char* mkvmerge_bin;
    char rate[64];
    char lang[64];
    char cmd[12288];
    char quoted_tool[2048];
    char quoted_output[2048];
    char quoted_track[2048];
    char quoted_intermediate[2048];
    char temp_output[1200];
    char timing_arg[128];
    char lang_arg[96];

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

    if (!build_unique_temp_output_path(opts->output_file, temp_output, sizeof(temp_output))) {
        emit_error(callbacks, "post-mux failed: could not generate temporary output path", ERR_INVALID_OPTIONS);
        return ERR_INVALID_OPTIONS;
    }
    platform_unlink(temp_output);

    timing_arg[0] = '\0';
    if (video_track_needs_forced_fps(opts->video_track_file)) {
        if (!probe_video_rate_string(ffprobe_bin, opts->intermediate_file, rate, sizeof(rate))) {
            emit_error(callbacks, "post-mux failed: could not probe source FPS", ERR_FFPROBE_FAILED);
            return ERR_FFPROBE_FAILED;
        }
        snprintf(timing_arg, sizeof(timing_arg), "--default-duration 0:%sfps ", rate);
    }

    /* The replacement video track is usually a raw elementary stream with no
     * language tag of its own, so inherit it from the original source. */
    lang_arg[0] = '\0';
    if (probe_video_track_language(ffprobe_bin, opts->intermediate_file, lang, sizeof(lang)))
        snprintf(lang_arg, sizeof(lang_arg), "--language 0:%s ", lang);

    platform_shell_quote(mkvmerge_bin, quoted_tool, sizeof(quoted_tool));
    platform_shell_quote(temp_output, quoted_output, sizeof(quoted_output));
    platform_shell_quote(opts->video_track_file, quoted_track, sizeof(quoted_track));
    platform_shell_quote(opts->intermediate_file, quoted_intermediate, sizeof(quoted_intermediate));

    snprintf(cmd,
             sizeof(cmd),
             "%s -o %s --no-audio --no-subtitles --no-buttons --no-attachments "
             "--no-chapters --no-global-tags --no-track-tags %s%s--video-tracks 0 %s --no-video %s 2>&1",
             quoted_tool,
             quoted_output,
             timing_arg,
             lang_arg,
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

    if (platform_rename(temp_output, opts->output_file) != 0) {
        platform_unlink(temp_output);
        emit_error(callbacks, "post-mux failed: could not move validated output into place", ERR_UNKNOWN);
        return ERR_UNKNOWN;
    }

    emit_message(callbacks, "Post-mux completed");
    return ERR_OK;
}

/* Stream-copy remux of a finished mkvmerge output into another container
 * (used for the mux "mov" preset). */
static ConverterError mux_remux_container(const char* input_file,
                                          const char* output_file,
                                          const char* container_fmt,
                                          const ConverterCallbacks* callbacks)
{
    const char* ffmpeg_bin = platform_get_ffmpeg_bin();
    char cmd[8192];
    char quoted_tool[2048];
    char quoted_input[2048];
    char quoted_output[2048];

    if (!ffmpeg_bin || ffmpeg_bin[0] == '\0') {
        emit_error(callbacks, "post-mux failed: ffmpeg not found", ERR_INVALID_OPTIONS);
        return ERR_INVALID_OPTIONS;
    }

    platform_shell_quote(ffmpeg_bin, quoted_tool, sizeof(quoted_tool));
    platform_shell_quote(input_file, quoted_input, sizeof(quoted_input));
    platform_shell_quote(output_file, quoted_output, sizeof(quoted_output));

    snprintf(cmd, sizeof(cmd), "%s -y -nostdin -i %s -c copy -f %s %s 2>&1",
             quoted_tool, quoted_input, container_fmt, quoted_output);

    emit_stage(callbacks, "Post-mux: remux to .mov");
    if (run_mux_command(cmd, callbacks) != 0) {
        emit_error(callbacks, "post-mux failed: container remux returned error", ERR_FFMPEG_FAILED);
        return ERR_FFMPEG_FAILED;
    }
    return ERR_OK;
}

/* Preset-aware entry point for the "mux" codec: opts->output_file is ignored
 * and computed internally from final_output_file + convert_opts->preset.
 *   "mkv" (default/unset) — unchanged mkvmerge-in-place behavior.
 *   "mov" — mkvmerge into a temp .mkv, then ffmpeg stream-copy remux to .mov.
 *   "m4v" — mkvmerge into a temp .mkv, then the Apple M4V pipeline on it. */
ConverterError mux_run_postprocess_for_preset(
    const MuxOptions* opts,
    const ConvertOptions* convert_opts,
    const char* final_output_file,
    const ConverterCallbacks* callbacks
) {
    MuxOptions local_opts;
    const char* preset;
    char temp_mkv[1200];
    ConverterError err;

    if (!opts || !final_output_file)
        return ERR_INVALID_OPTIONS;

    preset = (convert_opts && convert_opts->preset[0] != '\0') ? convert_opts->preset : "mkv";
    local_opts = *opts;

    if (!strcmp(preset, "mkv")) {
        strncpy(local_opts.output_file, final_output_file, sizeof(local_opts.output_file) - 1);
        local_opts.output_file[sizeof(local_opts.output_file) - 1] = '\0';
        return mux_run_postprocess(&local_opts, convert_opts, callbacks);
    }

    snprintf(temp_mkv, sizeof(temp_mkv), "%s.mux_tmp.mkv", final_output_file);
    strncpy(local_opts.output_file, temp_mkv, sizeof(local_opts.output_file) - 1);
    local_opts.output_file[sizeof(local_opts.output_file) - 1] = '\0';

    err = mux_run_postprocess(&local_opts, convert_opts, callbacks);
    if (err != ERR_OK)
        return err;

    if (!strcmp(preset, "mov")) {
        err = mux_remux_container(temp_mkv, final_output_file, "mov", callbacks);
    } else if (!strcmp(preset, "m4v")) {
        M4VOptions m4v_opts;
        char detail[256];

        emit_stage(callbacks, "Post-mux: Apple M4V pipeline");
        m4v_default_options(&m4v_opts);
        detail[0] = '\0';
        err = m4v_create_from_input(temp_mkv, final_output_file, &m4v_opts,
                                   opts->overwrite, NULL, callbacks,
                                   detail, sizeof(detail));
        if (err != ERR_OK)
            emit_error(callbacks, detail[0] != '\0' ? detail : "Apple M4V pipeline failed", err);
    } else {
        /* Unrecognized preset value: keep the mkvmerge result rather than
         * silently discarding a completed mux. */
        if (platform_rename(temp_mkv, final_output_file) != 0) {
            emit_error(callbacks, "post-mux failed: could not finalize output", ERR_UNKNOWN);
            err = ERR_UNKNOWN;
        } else {
            err = ERR_OK;
        }
    }

    platform_unlink(temp_mkv);
    return err;
}
