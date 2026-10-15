#include "mux.h"
#include "converter_platform.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static void expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static int write_fixture(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    int ok;
    if (!file)
        return 0;
    ok = fputs(text, file) >= 0;
    return fclose(file) == 0 && ok;
}

static int tool_available(const char *tool, const char *version_option)
{
    char cmd[4096];
    char *quoted;
    int status;
    if (!tool || !tool[0])
        return 0;
    quoted = platform_escape_path_for_command(tool);
    if (!quoted)
        return 0;
    snprintf(cmd, sizeof(cmd), "%s %s >/dev/null 2>&1", quoted, version_option);
    free(quoted);
    status = system(cmd);
    return status == 0;
}

static json_t *probe(const char *ffprobe, const char *path)
{
    char cmd[4096];
    char *quoted = platform_escape_path_for_command(ffprobe);
    FILE *pipe;
    json_t *result;
    if (!quoted)
        return NULL;
    snprintf(cmd, sizeof(cmd),
             "%s -v error -show_streams -show_chapters -show_format -of json '%s'",
             quoted, path);
    free(quoted);
    pipe = popen(cmd, "r");
    if (!pipe)
        return NULL;
    result = json_loadf(pipe, 0, NULL);
    if (pclose(pipe) != 0) {
        json_decref(result);
        return NULL;
    }
    return result;
}

static int create_media(const char *ffmpeg, const char *dir, const char *kind,
                        int width)
{
    char cmd[8192];
    char path[512];
    char text[1024];
    char *quoted;
    snprintf(path, sizeof(path), "%s/%s.srt", dir, kind);
    snprintf(text, sizeof(text), "1\n00:00:00,000 --> 00:00:00,900\n%s_SUBTITLE\n",
             kind);
    if (!write_fixture(path, text))
        return 0;
    snprintf(path, sizeof(path), "%s/%s.meta", dir, kind);
    snprintf(text, sizeof(text),
             ";FFMETADATA1\ntitle=%s_TITLE\ncomment=%s_GLOBAL\n"
             "[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=900\ntitle=%s_CHAPTER\n",
             kind, kind, kind);
    if (!write_fixture(path, text))
        return 0;
    snprintf(path, sizeof(path), "%s/%s.txt", dir, kind);
    snprintf(text, sizeof(text), "%s_ATTACHMENT\n", kind);
    if (!write_fixture(path, text))
        return 0;
    quoted = platform_escape_path_for_command(ffmpeg);
    if (!quoted)
        return 0;
    snprintf(cmd, sizeof(cmd),
             "%s -hide_banner -loglevel error -nostdin -y "
             "-f lavfi -i color=size=%dx32:rate=10:duration=1 "
             "-f lavfi -i sine=frequency=440:duration=1 "
             "-i '%s/%s.srt' -f ffmetadata -i '%s/%s.meta' "
             "-map 0:v -map 1:a -map 1:a -map 2:s -map 2:s "
             "-map_metadata 3 -map_chapters 3 "
             "-c:v mpeg4 -c:a pcm_s16le -c:s srt "
             "-metadata:s:v:0 language=eng "
             "-metadata:s:v:0 isolation_marker=%s_VIDEO_TAG "
             "-metadata:s:a:0 isolation_marker=%s_AUDIO_TAG "
             "-metadata:s:s:0 title=%s_SUBTITLE_TRACK "
             "-attach '%s/%s.txt' -metadata:s:t:0 mimetype=text/plain "
             "'%s/%s.mkv'",
             quoted, width, dir, kind, dir, kind, kind, kind, kind,
             dir, kind, dir, kind);
    free(quoted);
    return system(cmd) == 0;
}

static void verify_output(const char *ffprobe, const char *path, int mov, int has_title)
{
    json_t *data = probe(ffprobe, path);
    json_t *streams;
    json_t *stream;
    size_t index;
    int video = 0, audio = 0, subtitles = 0, attachments = 0;
    char *text;
    expect(data != NULL, "probe muxed output");
    if (!data)
        return;
    streams = json_object_get(data, "streams");
    json_array_foreach(streams, index, stream) {
        const char *type = json_string_value(json_object_get(stream, "codec_type"));
        const char *codec = json_string_value(json_object_get(stream, "codec_name"));
        if (!type)
            continue;
        if (strcmp(type, "video") == 0) {
            ++video;
            expect(json_integer_value(json_object_get(stream, "width")) == 64,
                   "video comes from replacement");
        } else if (strcmp(type, "audio") == 0) {
            ++audio;
        } else if (strcmp(type, "subtitle") == 0) {
            ++subtitles;
            if (mov)
                expect(codec && strcmp(codec, "mov_text") == 0,
                       "MOV subtitles use compatible text codec");
        } else if (strcmp(type, "attachment") == 0) {
            ++attachments;
        }
    }
    expect(video == 1, "one replacement video");
    expect(audio == 2, "all source audio tracks, no replacement audio");
    expect(subtitles == 2, "all source subtitles, no replacement subtitles");
    expect(attachments == (mov ? 0 : 1),
           "only source attachment in MKV; incompatible attachment omitted in MOV");
    expect(json_array_size(json_object_get(data, "chapters")) == 1,
           "only source chapter preserved");
    text = json_dumps(data, JSON_COMPACT);
    expect(text != NULL, "serialize output probe");
    if (text) {
        expect((strstr(text, "source_TITLE") != NULL) == has_title,
               "source title or its absence preserved");
        expect(strstr(text, "source_GLOBAL") != NULL, "source global metadata preserved");
        expect(strstr(text, "source_CHAPTER") != NULL, "source chapter title preserved");
        if (!mov) {
            expect(strstr(text, "source_AUDIO_TAG") != NULL, "source track tag preserved");
            expect(strstr(text, "source.txt") != NULL, "source attachment preserved");
        }
        expect(strstr(text, "replacement_") == NULL,
               "replacement chapters, attachments and metadata excluded");
        free(text);
    }
    json_decref(data);
}

static void verify_subtitle(const char *ffmpeg, const char *dir, const char *output)
{
    char cmd[4096], path[512], text[512];
    char *quoted = platform_escape_path_for_command(ffmpeg);
    FILE *file;
    if (!quoted) {
        expect(0, "quote ffmpeg");
        return;
    }
    snprintf(path, sizeof(path), "%s/extracted.srt", dir);
    snprintf(cmd, sizeof(cmd),
             "%s -hide_banner -loglevel error -nostdin -y -i '%s' "
             "-map 0:s:0 -c:s srt '%s'", quoted, output, path);
    free(quoted);
    expect(system(cmd) == 0, "extract preserved source subtitle");
    file = fopen(path, "r");
    expect(file != NULL, "read preserved subtitle");
    if (file) {
        size_t size = fread(text, 1, sizeof(text) - 1, file);
        text[size] = '\0';
        expect(strstr(text, "source_SUBTITLE") != NULL,
               "subtitle payload comes from source");
        expect(strstr(text, "replacement_SUBTITLE") == NULL,
               "replacement subtitle payload absent");
        fclose(file);
    }
}

int main(void)
{
    const char *ffmpeg = platform_get_ffmpeg_bin();
    const char *ffprobe = platform_get_ffprobe_bin();
    const char *mkvmerge = platform_get_mkvmerge_bin();
    const char *files[] = {
        "source.srt", "source.meta", "source.txt", "source.mkv",
        "replacement.srt", "replacement.meta", "replacement.txt", "replacement.mkv",
        "output.mkv", "output.mov", "output.mov.mux_tmp.mkv", "extracted.srt",
        "source_without_title.mkv", "output_without_title.mkv"
    };
    char dir[128], output[512], path[512];
    MuxOptions opts = {0};
    ConvertOptions convert_opts = {0};
    size_t i;
    int created;

    /* Exercise mux directly without initializing hardware probing. */
    if (!ffmpeg || !ffmpeg[0])
        setenv("FFMPEG", "ffmpeg", 1);
    if (!ffprobe || !ffprobe[0])
        setenv("FFPROBE", "ffprobe", 1);
    if (!mkvmerge || !mkvmerge[0]) {
        const char *configured = getenv("MKVMERGE_BIN");
        setenv("MKVMERGE", configured && configured[0] ? configured : "mkvmerge", 1);
    }
    ffmpeg = platform_get_ffmpeg_bin();
    ffprobe = platform_get_ffprobe_bin();
    mkvmerge = platform_get_mkvmerge_bin();
    if (!tool_available(ffmpeg, "-version") || !tool_available(ffprobe, "-version") ||
        !tool_available(mkvmerge, "--version")) {
        puts("SKIP: ffmpeg, ffprobe and mkvmerge are required");
        return 77;
    }
    /* Keep all fixtures under the CTest working directory, never system temp. */
    snprintf(dir, sizeof(dir), "mux-replacement-fixtures-%lu", (unsigned long)getpid());
    if (mkdir(dir, 0700) != 0) {
        perror("mkdir fixtures");
        return 1;
    }
    created = create_media(ffmpeg, dir, "source", 32) &&
              create_media(ffmpeg, dir, "replacement", 64);
    expect(created, "create source and replacement media fixtures");
    if (created) {
        snprintf(opts.intermediate_file, sizeof(opts.intermediate_file), "%s/source.mkv", dir);
        snprintf(opts.video_track_file, sizeof(opts.video_track_file), "%s/replacement.mkv", dir);
        snprintf(opts.output_file, sizeof(opts.output_file), "%s/output.mkv", dir);
        opts.overwrite = 1;
        if (mux_run_postprocess(&opts, &convert_opts, NULL) == ERR_OK) {
            verify_output(ffprobe, opts.output_file, 0, 1);
            verify_subtitle(ffmpeg, dir, opts.output_file);
        } else {
            expect(0, "MKV replacement mux succeeds");
        }
        snprintf(convert_opts.preset, sizeof(convert_opts.preset), "mov");
        snprintf(convert_opts.audio_output_mode, sizeof(convert_opts.audio_output_mode), "pcm");
        snprintf(output, sizeof(output), "%s/output.mov", dir);
        if (mux_run_postprocess_for_preset(&opts, &convert_opts, output, NULL) == ERR_OK) {
            verify_output(ffprobe, output, 1, 1);
            verify_subtitle(ffmpeg, dir, output);
        } else {
            expect(0, "MOV replacement mux succeeds");
        }
        {
            char cmd[4096];
            char *quoted = platform_escape_path_for_command(ffmpeg);
            expect(quoted != NULL, "quote title-removal tool");
            if (quoted) {
                snprintf(cmd, sizeof(cmd),
                         "%s -hide_banner -loglevel error -nostdin -y "
                         "-i '%s/source.mkv' -map 0 -c copy -metadata title= "
                         "'%s/source_without_title.mkv'", quoted, dir, dir);
                free(quoted);
                expect(system(cmd) == 0, "create source without segment title");
                snprintf(opts.intermediate_file, sizeof(opts.intermediate_file),
                         "%s/source_without_title.mkv", dir);
                snprintf(opts.output_file, sizeof(opts.output_file),
                         "%s/output_without_title.mkv", dir);
                if (mux_run_postprocess(&opts, &convert_opts, NULL) == ERR_OK)
                    verify_output(ffprobe, opts.output_file, 0, 0);
                else
                    expect(0, "replacement mux with untitled source succeeds");
            }
        }
    }
    for (i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s", dir, files[i]);
        unlink(path);
    }
    expect(rmdir(dir) == 0, "remove all media fixtures");
    if (failures)
        return 1;
    puts("replacement-video isolation tests passed");
    return 0;
}
