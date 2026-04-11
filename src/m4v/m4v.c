#include "m4v.h"

#include <jansson.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "linux/runtime_probe.h"

static void emit_message(const ConverterCallbacks *cb, const char *text)
{
    if (cb && cb->on_message && text)
        cb->on_message(text);
}

static void emit_stage(const ConverterCallbacks *cb, const char *text)
{
    if (cb && cb->on_stage && text)
        cb->on_stage(text);
}

static void emit_error(const ConverterCallbacks *cb, const char *text, ConverterError err)
{
    if (cb && cb->on_error && text)
        cb->on_error(text, err);
}

static void copy_string(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || dst_sz == 0)
        return;

    if (!src)
        src = "";

    strncpy(dst, src, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
}

static int is_regular_file(const char *path)
{
    struct stat st;

    return path &&
           path[0] != '\0' &&
           stat(path, &st) == 0 &&
           S_ISREG(st.st_mode) &&
           access(path, R_OK) == 0;
}

static void shell_quote(const char *input, char *out, size_t out_sz)
{
    size_t out_pos = 0;

    if (!out || out_sz == 0)
        return;

    if (!input)
        input = "";

    if (out_sz < 3) {
        out[0] = '\0';
        return;
    }

    out[out_pos++] = '\'';
    while (*input && out_pos + 5 < out_sz) {
        if (*input == '\'') {
            out[out_pos++] = '\'';
            out[out_pos++] = '\\';
            out[out_pos++] = '\'';
            out[out_pos++] = '\'';
        } else {
            out[out_pos++] = *input;
        }
        input++;
    }
    out[out_pos++] = '\'';
    out[out_pos] = '\0';
}

static void shell_quote_double(const char *input, char *out, size_t out_sz)
{
    size_t out_pos = 0;

    if (!out || out_sz == 0)
        return;

    if (!input)
        input = "";

    if (out_sz < 3) {
        out[0] = '\0';
        return;
    }

    out[out_pos++] = '"';
    while (*input && out_pos + 3 < out_sz) {
        if (*input == '"' || *input == '\\' || *input == '$' || *input == '`')
            out[out_pos++] = '\\';
        out[out_pos++] = *input;
        input++;
    }
    out[out_pos++] = '"';
    out[out_pos] = '\0';
}

static int run_command_capture(const char *cmd,
                               char *output,
                               size_t output_sz,
                               const ConverterCallbacks *cb,
                               volatile int *stop_flag)
{
    FILE *fp;
    char line[1024];
    size_t used = 0;
    int status;

    fp = popen(cmd, "r");
    if (!fp)
        return -1;

    if (output && output_sz > 0)
        output[0] = '\0';

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0')
            emit_message(cb, line);

        if (output && output_sz > 1 && used + len < output_sz) {
            memcpy(output + used, line, len);
            used += len;
            output[used++] = '\n';
            output[used] = '\0';
        }

        if (stop_flag && *stop_flag) {
            pclose(fp);
            return -2;
        }
    }

    status = pclose(fp);
    if (status == -1)
        return -1;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

static double parse_rate_to_fps(const char *text)
{
    double n;
    double d;

    if (!text || text[0] == '\0' || strcmp(text, "0/0") == 0)
        return 25.0;

    if (sscanf(text, "%lf/%lf", &n, &d) == 2) {
        if (d != 0.0)
            return n / d;
        return 25.0;
    }

    n = strtod(text, NULL);
    if (n > 0.0)
        return n;

    return 25.0;
}

static double probe_fps_for_input(const char *ffprobe_bin, const char *input_file)
{
    char quoted_tool[2048];
    char quoted_input[2048];
    char cmd[8192];
    char output[512];
    int rc;

    shell_quote(ffprobe_bin, quoted_tool, sizeof(quoted_tool));
    shell_quote(input_file, quoted_input, sizeof(quoted_input));

    snprintf(cmd,
             sizeof(cmd),
             "%s -v error -select_streams v:0 -show_entries stream=avg_frame_rate -of default=noprint_wrappers=1:nokey=1 %s 2>/dev/null",
             quoted_tool,
             quoted_input);
    rc = run_command_capture(cmd, output, sizeof(output), NULL, NULL);
    if (rc == 0) {
        output[strcspn(output, "\r\n")] = '\0';
        return parse_rate_to_fps(output);
    }

    snprintf(cmd,
             sizeof(cmd),
             "%s -v error -select_streams v:0 -show_entries stream=r_frame_rate -of default=noprint_wrappers=1:nokey=1 %s 2>/dev/null",
             quoted_tool,
             quoted_input);
    rc = run_command_capture(cmd, output, sizeof(output), NULL, NULL);
    if (rc == 0) {
        output[strcspn(output, "\r\n")] = '\0';
        return parse_rate_to_fps(output);
    }

    return 25.0;
}

static void make_chapter_timestamp(double seconds, char *out, size_t out_sz)
{
    int hours;
    int minutes;
    int whole_seconds;
    int millis;

    if (!out || out_sz == 0)
        return;

    if (seconds < 0.0)
        seconds = 0.0;

    hours = (int)(seconds / 3600.0);
    seconds -= (double)hours * 3600.0;
    minutes = (int)(seconds / 60.0);
    seconds -= (double)minutes * 60.0;
    whole_seconds = (int)seconds;
    millis = (int)((seconds - (double)whole_seconds) * 1000.0 + 0.5);
    if (millis == 1000) {
        whole_seconds += 1;
        millis = 0;
    }

    snprintf(out, out_sz, "%d:%02d:%02d.%03d", hours, minutes, whole_seconds, millis);
}

static int build_chapter_text_from_json(const char *json_text, const char *chapters_txt)
{
    json_error_t err;
    json_t *root;
    json_t *chapters;
    FILE *fp;
    size_t index;
    json_t *item;
    int wrote = 0;

    if (!json_text || !chapters_txt)
        return 0;

    root = json_loads(json_text, 0, &err);
    if (!root)
        return 0;

    chapters = json_object_get(root, "chapters");
    if (!json_is_array(chapters) || json_array_size(chapters) == 0) {
        json_decref(root);
        return 0;
    }

    fp = fopen(chapters_txt, "w");
    if (!fp) {
        json_decref(root);
        return 0;
    }

    json_array_foreach(chapters, index, item) {
        json_t *start_time;
        json_t *tags;
        json_t *title_obj;
        const char *title = NULL;
        char timestamp[64];
        double start_seconds = 0.0;

        if (!json_is_object(item))
            continue;

        start_time = json_object_get(item, "start_time");
        if (json_is_string(start_time))
            start_seconds = strtod(json_string_value(start_time), NULL);
        else if (json_is_number(start_time))
            start_seconds = json_number_value(start_time);

        tags = json_object_get(item, "tags");
        if (json_is_object(tags)) {
            title_obj = json_object_get(tags, "title");
            if (json_is_string(title_obj))
                title = json_string_value(title_obj);
        }

        if (!title || title[0] == '\0')
            title = "Chapter";

        make_chapter_timestamp(start_seconds, timestamp, sizeof(timestamp));
        fprintf(fp, "%s %s\n", timestamp, title);
        wrote = 1;
    }

    fclose(fp);
    json_decref(root);
    return wrote;
}

static int make_temp_dir(char *path, size_t path_sz)
{
    char templ[1024];
    char *made;

    if (!path || path_sz == 0)
        return 0;

    snprintf(templ, sizeof(templ), "/tmp/m4v_mux_XXXXXX");
    made = mkdtemp(templ);
    if (!made)
        return 0;

    copy_string(path, path_sz, made);
    return 1;
}

static void remove_temp_dir(const char *dir)
{
    char cmd[2048];
    char quoted[1536];

    if (!dir || dir[0] == '\0')
        return;

    shell_quote(dir, quoted, sizeof(quoted));
    snprintf(cmd, sizeof(cmd), "rm -rf %s", quoted);
    system(cmd);
}

void m4v_default_options(M4VOptions *opts)
{
    if (!opts)
        return;

    memset(opts, 0, sizeof(*opts));
    opts->video_track_index = 0;
    opts->audio_track_index = 0;
    opts->aac_quality = 5;
    opts->ac3_bitrate_kbps = 640;
    opts->add_chapters = 1;
    copy_string(opts->audio_lang, sizeof(opts->audio_lang), "rus");
}

ConverterError m4v_make_output_name(const char *input_file,
                                    const char *output_dir,
                                    char *out_file,
                                    size_t out_file_sz)
{
    const char *name;
    const char *slash;
    char base[512];
    char *dot;

    if (!input_file || !out_file || out_file_sz == 0)
        return ERR_INVALID_OPTIONS;

    slash = strrchr(input_file, '/');
    name = slash ? slash + 1 : input_file;
    copy_string(base, sizeof(base), name);
    dot = strrchr(base, '.');
    if (dot)
        *dot = '\0';

    if (output_dir && output_dir[0] != '\0')
        snprintf(out_file, out_file_sz, "%s/%s.m4v", output_dir, base);
    else
        snprintf(out_file, out_file_sz, "%s.m4v", base);

    return ERR_OK;
}

ConverterError m4v_validate_input_supported(const char *input_file,
                                            char *detail,
                                            size_t detail_sz)
{
    const char *ffprobe_bin;
    char quoted_tool[2048];
    char quoted_input[2048];
    char cmd[8192];
    char output[512];
    int rc;

    if (detail && detail_sz > 0)
        detail[0] = '\0';

    if (!is_regular_file(input_file)) {
        copy_string(detail, detail_sz, "input file not found or unreadable");
        return ERR_INPUT_NOT_READABLE;
    }

    ffprobe_bin = linux_get_preferred_ffprobe_bin();
    shell_quote(ffprobe_bin, quoted_tool, sizeof(quoted_tool));
    shell_quote(input_file, quoted_input, sizeof(quoted_input));
    snprintf(cmd,
             sizeof(cmd),
             "%s -v error -select_streams v:0 -show_entries stream=codec_name -of default=noprint_wrappers=1:nokey=1 %s 2>/dev/null",
             quoted_tool,
             quoted_input);

    rc = run_command_capture(cmd, output, sizeof(output), NULL, NULL);
    if (rc != 0) {
        copy_string(detail, detail_sz, "failed to probe video codec");
        return ERR_FFPROBE_FAILED;
    }

    output[strcspn(output, "\r\n")] = '\0';
    if (strcmp(output, "h264") != 0 && strcmp(output, "hevc") != 0 && strcmp(output, "prores") != 0) {
        snprintf(detail, detail_sz, "unsupported video codec for M4V: %s", output[0] != '\0' ? output : "unknown");
        return ERR_INVALID_OPTIONS;
    }

    snprintf(cmd,
             sizeof(cmd),
             "%s -v error -select_streams a:0 -show_entries stream=codec_type -of default=noprint_wrappers=1:nokey=1 %s 2>/dev/null",
             quoted_tool,
             quoted_input);
    rc = run_command_capture(cmd, output, sizeof(output), NULL, NULL);
    if (rc != 0 || strstr(output, "audio") == NULL) {
        copy_string(detail, detail_sz, "input file has no usable audio stream");
        return ERR_INVALID_OPTIONS;
    }

    return ERR_OK;
}

ConverterError m4v_create_from_input(const char *input_file,
                                     const char *output_file,
                                     const M4VOptions *opts,
                                     int overwrite,
                                     volatile int *stop_flag,
                                     const ConverterCallbacks *callbacks,
                                     char *error_text,
                                     size_t error_text_sz)
{
    const char *ffmpeg_bin;
    const char *ffprobe_bin;
    const char *mp4box_bin;
    M4VOptions local_opts;
    char work_dir[1024];
    char video_mp4[1200];
    char aac_m4a[1200];
    char ac3_mp4[1200];
    char chapters_json[1200];
    char chapters_txt[1200];
    char quoted_tool[2048];
    char quoted_input[2048];
    char quoted_output[2048];
    char quoted_video[2048];
    char quoted_aac[2048];
    char quoted_ac3[2048];
    char quoted_chapters[2048];
    char quoted_video_add[4096];
    char quoted_aac_add[4096];
    char quoted_ac3_add[4096];
    char video_add[3072];
    char aac_add[3072];
    char ac3_add[3072];
    char cmd[12288];
    char chapter_json[65536];
    char detail[256];
    int rc;
    double fps;
    const char *lang;

    if (error_text && error_text_sz > 0)
        error_text[0] = '\0';

    if (!input_file || !output_file)
        return ERR_INVALID_OPTIONS;

    if (opts)
        local_opts = *opts;
    else
        m4v_default_options(&local_opts);

    rc = m4v_validate_input_supported(input_file, detail, sizeof(detail));
    if (rc != ERR_OK) {
        copy_string(error_text, error_text_sz, detail);
        emit_error(callbacks, detail, (ConverterError)rc);
        return (ConverterError)rc;
    }

    ffmpeg_bin = linux_get_preferred_ffmpeg_bin();
    ffprobe_bin = linux_get_preferred_ffprobe_bin();
    mp4box_bin = linux_get_preferred_mp4box_bin();
    if (!ffmpeg_bin || !ffprobe_bin || !mp4box_bin ||
        ffmpeg_bin[0] == '\0' || ffprobe_bin[0] == '\0' || mp4box_bin[0] == '\0') {
        copy_string(error_text, error_text_sz, "Missing required tools (ffmpeg/ffprobe/MP4Box)");
        emit_error(callbacks, "Missing required tools (ffmpeg/ffprobe/MP4Box)", ERR_INVALID_OPTIONS);
        return ERR_INVALID_OPTIONS;
    }

    if (!make_temp_dir(work_dir, sizeof(work_dir))) {
        copy_string(error_text, error_text_sz, "Failed to create temp dir");
        emit_error(callbacks, "Failed to create temp dir", ERR_UNKNOWN);
        return ERR_UNKNOWN;
    }

    snprintf(video_mp4, sizeof(video_mp4), "%s/video_only.mp4", work_dir);
    snprintf(aac_m4a, sizeof(aac_m4a), "%s/audio_aac.m4a", work_dir);
    snprintf(ac3_mp4, sizeof(ac3_mp4), "%s/audio_ac3.mp4", work_dir);
    snprintf(chapters_json, sizeof(chapters_json), "%s/chapters.json", work_dir);
    snprintf(chapters_txt, sizeof(chapters_txt), "%s/chapters.txt", work_dir);

    fps = probe_fps_for_input(ffprobe_bin, input_file);
    lang = local_opts.audio_lang[0] != '\0' ? local_opts.audio_lang : "rus";

    shell_quote(ffmpeg_bin, quoted_tool, sizeof(quoted_tool));
    shell_quote(input_file, quoted_input, sizeof(quoted_input));
    shell_quote(video_mp4, quoted_video, sizeof(quoted_video));

    emit_stage(callbacks, "Apple M4V step 1/5: video copy");
    snprintf(cmd,
             sizeof(cmd),
             "%s -y -nostdin -i %s -map 0:v:%d -c:v copy -an -sn -dn -f mp4 %s 2>&1",
             quoted_tool,
             quoted_input,
             local_opts.video_track_index,
             quoted_video);
    rc = run_command_capture(cmd, NULL, 0, callbacks, stop_flag);
    if (rc != 0) {
        copy_string(error_text, error_text_sz, rc == -2 ? "Stopped" : "Apple M4V video copy failed");
        remove_temp_dir(work_dir);
        return rc == -2 ? ERR_SKIP_FILE : ERR_FFMPEG_FAILED;
    }

    shell_quote(aac_m4a, quoted_aac, sizeof(quoted_aac));
    emit_stage(callbacks, "Apple M4V step 2/5: AAC encode");
    snprintf(cmd,
             sizeof(cmd),
             "%s -y -nostdin -i %s -map 0:a:%d -c:a libfdk_aac -vbr %d -ar 48000 -f mp4 %s 2>&1",
             quoted_tool,
             quoted_input,
             local_opts.audio_track_index,
             local_opts.aac_quality,
             quoted_aac);
    rc = run_command_capture(cmd, NULL, 0, callbacks, stop_flag);
    if (rc != 0) {
        copy_string(error_text, error_text_sz, rc == -2 ? "Stopped" : "Apple M4V AAC encode failed");
        remove_temp_dir(work_dir);
        return rc == -2 ? ERR_SKIP_FILE : ERR_FFMPEG_FAILED;
    }

    shell_quote(ac3_mp4, quoted_ac3, sizeof(quoted_ac3));
    emit_stage(callbacks, "Apple M4V step 3/5: AC3 encode");
    snprintf(cmd,
             sizeof(cmd),
             "%s -y -nostdin -i %s -map 0:a:%d -c:a ac3 -b:a %dk -f mp4 %s 2>&1",
             quoted_tool,
             quoted_input,
             local_opts.audio_track_index,
             local_opts.ac3_bitrate_kbps,
             quoted_ac3);
    rc = run_command_capture(cmd, NULL, 0, callbacks, stop_flag);
    if (rc != 0) {
        copy_string(error_text, error_text_sz, rc == -2 ? "Stopped" : "Apple M4V AC3 encode failed");
        remove_temp_dir(work_dir);
        return rc == -2 ? ERR_SKIP_FILE : ERR_FFMPEG_FAILED;
    }

    shell_quote(mp4box_bin, quoted_tool, sizeof(quoted_tool));
    shell_quote(output_file, quoted_output, sizeof(quoted_output));
    emit_stage(callbacks, "Apple M4V step 4/5: MP4Box mux");
    snprintf(video_add, sizeof(video_add), "%s#video:fps=%.6f:name=Video", video_mp4, fps);
    snprintf(aac_add, sizeof(aac_add), "%s#audio:name=AAC:lang=%s", aac_m4a, lang);
    snprintf(ac3_add, sizeof(ac3_add), "%s#audio:name=AC3 %dk:lang=%s", ac3_mp4, local_opts.ac3_bitrate_kbps, lang);
    shell_quote_double(video_add, quoted_video_add, sizeof(quoted_video_add));
    shell_quote_double(aac_add, quoted_aac_add, sizeof(quoted_aac_add));
    shell_quote_double(ac3_add, quoted_ac3_add, sizeof(quoted_ac3_add));
    snprintf(cmd,
             sizeof(cmd),
             "%s -new -brand 'M4V :0' -ab mp42 -ab isom -add %s -add %s -add %s %s 2>&1",
             quoted_tool,
             quoted_video_add,
             quoted_aac_add,
             quoted_ac3_add,
             quoted_output);
    if (!overwrite && access(output_file, F_OK) == 0) {
        copy_string(error_text, error_text_sz, "Output exists (enable overwrite)");
        remove_temp_dir(work_dir);
        return ERR_OUTPUT_EXISTS;
    }
    if (overwrite)
        unlink(output_file);
    rc = run_command_capture(cmd, NULL, 0, callbacks, stop_flag);
    if (rc != 0) {
        copy_string(error_text, error_text_sz, rc == -2 ? "Stopped" : "Apple M4V MP4Box mux failed");
        remove_temp_dir(work_dir);
        return rc == -2 ? ERR_SKIP_FILE : ERR_FFMPEG_FAILED;
    }

    if (local_opts.add_chapters) {
        shell_quote(ffprobe_bin, quoted_tool, sizeof(quoted_tool));
        emit_stage(callbacks, "Apple M4V step 5/5: chapters");
        snprintf(cmd,
                 sizeof(cmd),
                 "%s -v error -print_format json -show_chapters %s 2>/dev/null",
                 quoted_tool,
                 quoted_input);
        rc = run_command_capture(cmd, chapter_json, sizeof(chapter_json), NULL, stop_flag);
        if (rc == 0) {
            FILE *json_fp = fopen(chapters_json, "w");
            if (json_fp) {
                fputs(chapter_json, json_fp);
                fclose(json_fp);
            }
            if (build_chapter_text_from_json(chapter_json, chapters_txt)) {
                shell_quote(mp4box_bin, quoted_tool, sizeof(quoted_tool));
                shell_quote(chapters_txt, quoted_chapters, sizeof(quoted_chapters));
                snprintf(cmd,
                         sizeof(cmd),
                         "%s -chap %s %s 2>&1",
                         quoted_tool,
                         quoted_chapters,
                         quoted_output);
                rc = run_command_capture(cmd, NULL, 0, callbacks, stop_flag);
                if (rc != 0 && rc != -2)
                    emit_message(callbacks, "Apple M4V chapters warning: chapter import failed");
            } else {
                emit_message(callbacks, "Apple M4V: no chapters found in source");
            }
        } else if (rc != -2) {
            emit_message(callbacks, "Apple M4V chapter probe warning: could not read chapters");
        }
    }

    remove_temp_dir(work_dir);
    if (stop_flag && *stop_flag) {
        unlink(output_file);
        copy_string(error_text, error_text_sz, "Stopped");
        return ERR_SKIP_FILE;
    }

    return ERR_OK;
}