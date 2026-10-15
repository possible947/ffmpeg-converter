#include "mux.h"
#include "converter_platform.h"
#include "add_track.h"
#include "mux_platform.h"

#include <jansson.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
static int language_injection_seen;
static int quoted_timing_seen;

static void test_log_message(const char *message)
{
    fprintf(stderr, "Add Track test: %s\n", message);
}

static void test_log_error(const char *message, ConverterError error)
{
    fprintf(stderr, "Add Track test error (%d): %s\n", error, message);
}

static void expect(int condition, const char *message);
static int write_fixture(const char *path, const char *text);
static json_t *probe(const char *ffprobe, const char *path);

static int transcode_video_fixture(const char *ffmpeg,
                                   const char *input,
                                   const char *output,
                                   const char *encoder,
                                   const char *encoder_options)
{
    char cmd[8192];
    char *quoted_tool = platform_escape_path_for_command(ffmpeg);
    if (!quoted_tool)
        return 0;
    snprintf(cmd, sizeof(cmd),
             "%s -hide_banner -loglevel error -nostdin -y -i '%s' -map 0 "
             "-c copy -c:v %s %s '%s'",
             quoted_tool, input, encoder, encoder_options, output);
    free(quoted_tool);
    return system(cmd) == 0;
}

static int create_prores_fixture(const char *ffmpeg,
                                const char *input,
                                const char *output)
{
    char cmd[8192];
    char *quoted_tool = platform_escape_path_for_command(ffmpeg);
    if (!quoted_tool)
        return 0;
    snprintf(cmd, sizeof(cmd),
             "%s -hide_banner -loglevel error -nostdin -y -i '%s' "
             "-map 0:v:0 -map 0:a:0 -map 0:s? -map_metadata 0 "
             "-map_chapters 0 -c:v prores_ks -profile:v 3 "
             "-c:a pcm_s16le -c:s mov_text '%s'",
             quoted_tool, input, output);
    free(quoted_tool);
    return system(cmd) == 0;
}

static ConverterError run_add_track(Converter *converter,
                                    const char *source,
                                    const char *replacement,
                                    const char *output_dir,
                                    const char *mode,
                                    const char *audio,
                                    AddTrackResult *result)
{
    ConvertOptions options = {0};
    AddTrackRequest request;
    snprintf(options.codec, sizeof(options.codec), "mux");
    snprintf(options.preset, sizeof(options.preset), "%s", mode);
    if (audio)
        snprintf(options.audio_output_mode, sizeof(options.audio_output_mode),
                 "%s", audio);
    snprintf(options.output_dir, sizeof(options.output_dir), "%s", output_dir);
    snprintf(options.video_track_path, sizeof(options.video_track_path), "%s",
             replacement);
    options.overwrite = 1;
    request.source_file = source;
    request.convert_options = &options;
    return add_track_execute(converter, &request, result);
}

static ConverterError run_hq_add_track(Converter *converter,
                                       const char *source,
                                       const char *replacement,
                                       const char *output_dir,
                                       const char *encoder,
                                       const char *audio,
                                       AddTrackResult *result)
{
    ConvertOptions options = {0};
    AddTrackRequest request;
    snprintf(options.codec, sizeof(options.codec), "hq_converter");
    snprintf(options.hw_device, sizeof(options.hw_device), "%s", encoder);
    snprintf(options.preset, sizeof(options.preset), "hq");
    if (audio)
        snprintf(options.audio_output_mode, sizeof(options.audio_output_mode),
                 "%s", audio);
    snprintf(options.output_dir, sizeof(options.output_dir), "%s", output_dir);
    snprintf(options.video_track_path, sizeof(options.video_track_path), "%s",
             replacement);
    options.overwrite = 1;
    request.source_file = source;
    request.convert_options = &options;
    return add_track_execute(converter, &request, result);
}

static int verify_production_audio(const char *ffprobe,
                                   const char *path,
                                   int expected_count,
                                   const char *first_codec,
                                   const char *second_codec)
{
    json_t *data = probe(ffprobe, path);
    json_t *streams;
    json_t *stream;
    size_t index;
    int count = 0;
    int codecs_match = 1;
    if (!data)
        return 0;
    streams = json_object_get(data, "streams");
    json_array_foreach(streams, index, stream) {
        const char *type = json_string_value(json_object_get(stream, "codec_type"));
        const char *codec = json_string_value(json_object_get(stream, "codec_name"));
        if (type && strcmp(type, "audio") == 0) {
            const char *expected = count == 0 ? first_codec : second_codec;
            if (!expected || !codec || strcmp(codec, expected) != 0)
                codecs_match = 0;
            ++count;
        }
    }
    json_decref(data);
    return count == expected_count && codecs_match;
}

static int verify_add_track_preservation(const char *ffprobe,
                                         const char *path,
                                         const char *video_codec,
                                         int expected_subtitles,
                                         int expected_attachments,
                                         const char *subtitle_codec)
{
    json_t *data = probe(ffprobe, path);
    json_t *streams;
    json_t *stream;
    size_t index;
    int video = 0;
    int video_codec_matches = 1;
    int subtitles = 0;
    int attachments = 0;
    int subtitle_codecs_match = 1;
    const char *title;
    const char *comment;
    char *serialized;
    int ok;
    if (!data)
        return 0;
    streams = json_object_get(data, "streams");
    json_array_foreach(streams, index, stream) {
        const char *type = json_string_value(json_object_get(stream, "codec_type"));
        const char *codec = json_string_value(json_object_get(stream, "codec_name"));
        if (type && strcmp(type, "video") == 0) {
            ++video;
            if (!codec || strcmp(codec, video_codec) != 0)
                video_codec_matches = 0;
        } else if (type && strcmp(type, "subtitle") == 0) {
            ++subtitles;
            if (!codec || strcmp(codec, subtitle_codec) != 0)
                subtitle_codecs_match = 0;
        } else if (type && strcmp(type, "attachment") == 0) {
            ++attachments;
        }
    }
    title = json_string_value(json_object_get(
        json_object_get(json_object_get(data, "format"), "tags"), "title"));
    serialized = json_dumps(data, JSON_COMPACT);
    ok = video == 1 &&
         video_codec_matches &&
         subtitles == expected_subtitles &&
         subtitle_codecs_match &&
         attachments == expected_attachments &&
         json_array_size(json_object_get(data, "chapters")) == 1 &&
         title && strcmp(title, "source_TITLE") == 0 &&
         serialized && strstr(serialized, "source_GLOBAL") != NULL &&
         serialized && strstr(serialized, "replacement_") == NULL;
    if (!ok)
        fprintf(stderr,
                "preservation details for %s: video=%d subtitles=%d/%d "
                "subtitle-codecs=%d attachments=%d/%d chapters=%zu "
                "title=%s comment=%s replacement=%d\n",
                path, video, subtitles, expected_subtitles, subtitle_codecs_match,
                attachments, expected_attachments,
                json_array_size(json_object_get(data, "chapters")),
                title ? title : "(missing)",
                serialized && strstr(serialized, "source_GLOBAL")
                    ? "source_GLOBAL" : "(missing)",
                serialized && strstr(serialized, "replacement_") != NULL);
    free(serialized);
    json_decref(data);
    return ok;
}

static void test_production_add_track(const char *ffmpeg,
                                     const char *ffprobe,
                                     const char *dir,
                                     const char *source,
                                     const char *replacement)
{
    static const struct {
        const char *audio;
        int count;
        const char *first;
        const char *second;
    } mkv_cases[] = {
        {"copy", 2, "pcm_s16le", "pcm_s16le"},
        {"pcm", 1, "pcm_s16le", NULL},
        {"aac", 1, "aac", NULL},
        {"aac+ac3", 2, "aac", "ac3"}
    };
    Converter *converter = converter_create();
    AddTrackResult result;
    char output[1024];
    char source_h264[512];
    char source_av1[512];
    char replacement_h264[512];
    char replacement_av1[512];
    char replacement_hevc[512];
    char source_prores[512];
    char special_dir[640];
    char corrupt_source[512];
    char failed_temp_output[1024];
    size_t i;

    expect(converter != NULL, "create converter for production Add Track");
    if (!converter)
        return;
    snprintf(source_h264, sizeof(source_h264), "%s/source-h264.mkv", dir);
    snprintf(source_av1, sizeof(source_av1), "%s/source-av1.mkv", dir);
    snprintf(replacement_h264, sizeof(replacement_h264),
             "%s/replacement-h264.mkv", dir);
    snprintf(replacement_av1, sizeof(replacement_av1),
             "%s/replacement-av1.mkv", dir);
    snprintf(replacement_hevc, sizeof(replacement_hevc),
             "%s/replacement-hevc.mkv", dir);
    snprintf(source_prores, sizeof(source_prores), "%s/source-prores.mov", dir);

    expect(transcode_video_fixture(ffmpeg, source, source_h264, "libx264",
                                   "-preset ultrafast"),
           "create H.264 Add Track source");
    expect(transcode_video_fixture(ffmpeg, source_h264, source_av1, "libaom-av1",
                                   "-cpu-used 8 -crf 40 -b:v 0"),
           "create AV1 Add Track source");
    expect(transcode_video_fixture(ffmpeg, replacement, replacement_h264,
                                   "libx264", "-preset ultrafast"),
           "create H.264 replacement track");
    expect(transcode_video_fixture(ffmpeg, replacement, replacement_av1,
                                   "libaom-av1",
                                   "-cpu-used 8 -crf 40 -b:v 0"),
           "create AV1 replacement track");
    expect(transcode_video_fixture(ffmpeg, replacement, replacement_hevc,
                                   "libx265", "-preset ultrafast"),
           "create HEVC replacement track");
    expect(create_prores_fixture(ffmpeg, source, source_prores),
           "create ProRes Add Track source");

    if (platform_file_is_regular(source_h264) &&
        platform_file_is_regular(replacement_h264)) {
        for (i = 0; i < sizeof(mkv_cases) / sizeof(mkv_cases[0]); ++i) {
            ConverterError err = run_add_track(converter, source_h264,
                                               replacement_h264, dir, "mkv",
                                               mkv_cases[i].audio, &result);
            expect(err == ERR_OK, "production MKV Add Track succeeds");
            if (err == ERR_OK)
                expect(verify_production_audio(ffprobe, result.output_file,
                                               mkv_cases[i].count,
                                               mkv_cases[i].first,
                                               mkv_cases[i].second),
                       "production MKV audio policy is applied");
            if (err == ERR_OK && i == 0)
                expect(verify_add_track_preservation(ffprobe,
                                                     result.output_file,
                                                     "h264",
                                                     2, 1, "subrip"),
                       "production MKV preserves only source chapters, metadata, subtitles and attachments");
            platform_unlink(result.output_file);
        }

        {
            ConverterError err = run_add_track(converter, source_h264,
                                               replacement_h264, dir, "mov",
                                               "pcm", &result);
            expect(err == ERR_OK, "production MOV Add Track succeeds");
            if (err == ERR_OK)
                expect(verify_production_audio(ffprobe, result.output_file,
                                               1, "pcm_s16le", NULL),
                       "production MOV contains selected PCM audio");
            if (err == ERR_OK)
                expect(verify_add_track_preservation(ffprobe,
                                                     result.output_file,
                                                     "h264",
                                                     2, 0, "mov_text"),
                       "production MOV preserves compatible source streams and metadata");
            platform_unlink(result.output_file);
        }

        for (i = 0; i < 3; ++i) {
            const char *audio = i == 0 ? "aac" :
                                i == 1 ? "aac+ac3" : NULL;
            int count = i == 0 ? 1 : 2;
            ConverterError err = run_add_track(converter, source_h264,
                                               replacement_h264, dir, "m4v",
                                               audio, &result);
            expect(err == ERR_OK, "production M4V Add Track succeeds");
            if (err == ERR_OK)
                expect(verify_production_audio(ffprobe, result.output_file,
                                               count, "aac",
                                               count == 2 ? "ac3" : NULL),
                       "production M4V audio mode is applied");
            if (err == ERR_OK && i == 0)
                expect(verify_add_track_preservation(ffprobe,
                                                     result.output_file,
                                                     "h264",
                                                     2, 0, "mov_text"),
                       "production M4V preserves source subtitles, chapters and metadata");
            platform_unlink(result.output_file);
        }
    }

    if (platform_file_is_regular(source_av1) &&
        platform_file_is_regular(replacement_h264)) {
        ConverterError err = run_add_track(converter, source_av1,
                                           replacement_h264, dir, "mov",
                                           "pcm", &result);
        expect(err == ERR_OK, "AV1 source is replaced before MOV preparation");
        if (err == ERR_OK)
            expect(verify_production_audio(ffprobe, result.output_file,
                                           1, "pcm_s16le", NULL),
                   "AV1-source MOV contains PCM");
        if (err == ERR_OK)
            expect(verify_add_track_preservation(ffprobe, result.output_file,
                                                 "h264",
                                                 2, 0, "mov_text"),
                   "AV1-source MOV preserves supported source streams");
        platform_unlink(result.output_file);
    }

    if (platform_file_is_regular(source_prores) &&
        platform_file_is_regular(replacement_h264)) {
        for (i = 0; i < 3; ++i) {
            const char *audio = i == 0 ? "aac" :
                                i == 1 ? "aac+ac3" : NULL;
            int count = i == 0 ? 1 : 2;
            ConverterError err = run_add_track(converter, source_prores,
                                               replacement_h264, dir, "m4v",
                                               audio, &result);
            expect(err == ERR_OK, "ProRes source reaches production M4V Creator");
            if (err == ERR_OK)
                expect(verify_production_audio(ffprobe, result.output_file,
                                               count, "aac",
                                               count == 2 ? "ac3" : NULL),
                       "ProRes-source M4V audio mode is applied");
            if (err == ERR_OK && i == 0)
                expect(verify_add_track_preservation(ffprobe,
                                                     result.output_file,
                                                     "h264",
                                                     2, 0, "mov_text"),
                       "ProRes-source M4V preserves supported source streams");
            platform_unlink(result.output_file);
        }
    }

    if (platform_file_is_regular(source_h264) &&
        platform_file_is_regular(source_av1) &&
        platform_file_is_regular(source_prores)) {
        const char *hq_sources[] = {source_h264, source_av1};
        for (i = 0; i < sizeof(hq_sources) / sizeof(hq_sources[0]); ++i) {
            ConverterError err = run_hq_add_track(
                converter, hq_sources[i], source_prores, dir, "prores_ks",
                "aac", &result);
            expect(err == ERR_OK,
                   "HQ ProRes handoff resolves MOV+PCM after source preparation");
            if (err == ERR_OK) {
                expect(verify_production_audio(ffprobe, result.output_file,
                                               1, "pcm_s16le", NULL),
                       "HQ ProRes policy forces PCM");
                expect(verify_add_track_preservation(ffprobe,
                                                     result.output_file,
                                                     "prores",
                                                     2, 0, "mov_text"),
                       "HQ ProRes handoff preserves supported source streams");
            }
            platform_unlink(result.output_file);
        }
    }

    if (platform_file_is_regular(source_h264) &&
        platform_file_is_regular(replacement_h264) &&
        platform_file_is_regular(replacement_hevc)) {
        static const struct {
            const char *encoder;
            const char *replacement;
            const char *video_codec;
        } hq_cases[] = {
            {"x264", "replacement-h264.mkv", "h264"},
            {"x265", "replacement-hevc.mkv", "hevc"}
        };
        for (i = 0; i < sizeof(hq_cases) / sizeof(hq_cases[0]); ++i) {
            char track[512];
            snprintf(track, sizeof(track), "%s/%s", dir,
                     hq_cases[i].replacement);
            ConverterError err = run_hq_add_track(
                converter, source_h264, track, dir, hq_cases[i].encoder,
                "aac", &result);
            expect(err == ERR_OK, "HQ x264/x265 uses shared MKV Add Track");
            if (err == ERR_OK) {
                expect(verify_production_audio(ffprobe, result.output_file,
                                               1, "aac", NULL),
                       "HQ MKV handoff applies selected audio mode");
                expect(verify_add_track_preservation(ffprobe,
                                                     result.output_file,
                                                     hq_cases[i].video_codec,
                                                     2, 1, "subrip"),
                       "HQ MKV handoff keeps source streams and excludes replacement auxiliaries");
            }
            platform_unlink(result.output_file);
        }
    }

    if (platform_file_is_regular(replacement_av1)) {
        ConverterError err = run_add_track(converter, source_h264,
                                           replacement_av1, dir, "mkv",
                                           "pcm", &result);
        expect(err == ERR_OK, "production MKV Add Track accepts AV1 replacement");
        if (err == ERR_OK) {
            json_t *data = probe(ffprobe, result.output_file);
            const char *codec = data
                ? json_string_value(json_object_get(
                      json_array_get(json_object_get(data, "streams"), 0),
                      "codec_name"))
                : NULL;
            expect(codec && strcmp(codec, "av1") == 0,
                   "production MKV retains AV1 replacement video");
            json_decref(data);
        }
        platform_unlink(result.output_file);
    }

    snprintf(special_dir, sizeof(special_dir), "%s/path space + (paren) \xE6\xBC\xA2",
             dir);
    expect(mkdir(special_dir, 0700) == 0 ||
           platform_stat_is_directory(special_dir),
           "create M4V output directory with URL-special characters");
    {
        ConverterCallbacks callbacks = {0};
        callbacks.on_message = test_log_message;
        callbacks.on_error = test_log_error;
        converter_set_callbacks(converter, &callbacks);
        ConverterError err = run_add_track(converter, source_h264,
                                           replacement_h264, special_dir, "m4v",
                                           "aac", &result);
        expect(err == ERR_OK, "M4V MP4Box importer accepts special path characters");
        if (err == ERR_OK)
            expect(verify_production_audio(ffprobe, result.output_file,
                                           1, "aac", NULL),
                   "special-path M4V output is structurally valid");
        converter_set_callbacks(converter, NULL);
    }

    snprintf(corrupt_source, sizeof(corrupt_source), "%s/broken.mkv", dir);
    expect(write_fixture(corrupt_source, "not a media file\n"),
           "create corrupt Add Track source");
    {
        ConverterError err = run_add_track(converter, corrupt_source,
                                           replacement_h264, dir, "mkv",
                                           "pcm", &result);
        expect(err != ERR_OK,
               "single-source preparation failure is propagated");
        snprintf(output, sizeof(output), "%s/broken_converted.mkv", dir);
        expect(!platform_file_is_regular(output),
               "failed source preparation is removed before post-mux");
        snprintf(failed_temp_output, sizeof(failed_temp_output),
                 "%s.mux_tmp.mkv", output);
        expect(!platform_file_is_regular(failed_temp_output),
               "failed source preparation never runs post-mux");
    }
    converter_destroy(converter);
}

static void capture_message(const char *message)
{
    if (strcmp(message, "LANGUAGE_INJECTION_EXECUTED") == 0)
        language_injection_seen = 1;
    if (strstr(message, "--default-duration '0:10/1fps' ") != NULL)
        quoted_timing_seen = 1;
}

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

static void remove_test_tree(const char *path)
{
    DIR *directory = opendir(path);
    struct dirent *entry;
    if (!directory) {
        unlink(path);
        return;
    }
    while ((entry = readdir(directory)) != NULL) {
        char child[2048];
        struct stat info;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        if (snprintf(child, sizeof(child), "%s/%s", path,
                     entry->d_name) >= (int)sizeof(child))
            continue;
        if (lstat(child, &info) == 0 && S_ISDIR(info.st_mode))
            remove_test_tree(child);
        else
            unlink(child);
    }
    closedir(directory);
    rmdir(path);
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
             "-metadata:s:v:0 language=%s "
             "-metadata:s:v:0 title=%s_VIDEO_TITLE "
             "-metadata:s:v:0 isolation_marker=%s_VIDEO_TAG "
             "-metadata:s:a:0 isolation_marker=%s_AUDIO_TAG "
             "-metadata:s:s:0 title=%s_SUBTITLE_TRACK "
             "-attach '%s/%s.txt' -metadata:s:t:0 mimetype=text/plain "
             "'%s/%s.mkv'",
             quoted, width, dir, kind, dir, kind,
             strcmp(kind, "source") == 0 ? "eng" : "fra",
             kind, kind, kind, kind,
             dir, kind, dir, kind);
    free(quoted);
    return system(cmd) == 0;
}

static void verify_output(const char *ffprobe, const char *path, int mov,
                          int has_title, int expected_subtitles, int has_video_title,
                          const char *expected_language)
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
            const char *language = json_string_value(json_object_get(
                json_object_get(stream, "tags"), "language"));
            ++video;
            expect(json_integer_value(json_object_get(stream, "width")) == 64,
                   "video comes from replacement");
            expect((language && strcmp(language, expected_language) == 0) ||
                   (!language && strcmp(expected_language, "und") == 0),
                   "replacement video inherits source language or unspecified default");
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
    expect(audio == 2, "MUX-T001: all source audio tracks, no replacement audio");
    expect(subtitles == expected_subtitles,
           "MUX-T013: supported source subtitles survive without replacement subtitles");
    expect(attachments == (mov ? 0 : 1),
           "only source attachment in MKV; incompatible attachment omitted in MOV");
    expect(json_array_size(json_object_get(data, "chapters")) == 1,
           "MUX-T012: only source chapter preserved");
    text = json_dumps(data, JSON_COMPACT);
    expect(text != NULL, "serialize output probe");
    if (text) {
        expect((strstr(text, "source_TITLE") != NULL) == has_title,
               "source title or its absence preserved");
        expect(strstr(text, "source_GLOBAL") != NULL, "source global metadata preserved");
        expect(strstr(text, "source_CHAPTER") != NULL, "source chapter title preserved");
        if (!mov) {
            expect((strstr(text, "source_VIDEO_TITLE") != NULL) == has_video_title,
                   "source video track title or its absence preserved");
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
    char dir[128], output[512], path[512];
    char source_path[512], replacement_path[512];
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
        snprintf(source_path, sizeof(source_path), "%s/source.mkv", dir);
        snprintf(replacement_path, sizeof(replacement_path),
                 "%s/replacement.mkv", dir);
        test_production_add_track(ffmpeg, ffprobe, dir,
                                  source_path, replacement_path);
        snprintf(opts.intermediate_file, sizeof(opts.intermediate_file), "%s/source.mkv", dir);
        snprintf(opts.video_track_file, sizeof(opts.video_track_file), "%s/replacement.mkv", dir);
        snprintf(opts.output_file, sizeof(opts.output_file), "%s/output.mkv", dir);
        opts.overwrite = 1;
        {
            ConverterError mux_result =
                mux_run_postprocess(&opts, &convert_opts, NULL);
            expect(mux_result == ERR_OK,
                   "MUX-T018: existing MKV replacement workflow succeeds");
            if (mux_result == ERR_OK) {
                verify_output(ffprobe, opts.output_file, 0, 1, 2, 1, "eng");
                verify_subtitle(ffmpeg, dir, opts.output_file);
            }
        }
        snprintf(convert_opts.preset, sizeof(convert_opts.preset), "mov");
        snprintf(convert_opts.audio_output_mode, sizeof(convert_opts.audio_output_mode), "pcm");
        snprintf(output, sizeof(output), "%s/output.mov", dir);
        if (mux_run_postprocess_for_preset(&opts, &convert_opts, output, NULL) == ERR_OK) {
            verify_output(ffprobe, output, 1, 1, 2, 1, "eng");
            verify_subtitle(ffmpeg, dir, output);
        } else {
            expect(0, "MOV replacement mux succeeds");
        }
        {
            ConverterCallbacks callbacks = {0};
            callbacks.on_message = capture_message;
            /* Exercise suffix-based timing arguments without requiring an
             * additional encoder; mkvmerge identifies the container by content. */
            snprintf(path, sizeof(path), "%s/replacement.h264", dir);
            expect(link(opts.video_track_file, path) == 0,
                   "create replacement alias that requests forced FPS");
            snprintf(opts.video_track_file, sizeof(opts.video_track_file), "%s", path);
            snprintf(opts.output_file, sizeof(opts.output_file),
                     "%s/output_forced_duration.mkv", dir);
            if (mux_run_postprocess(&opts, &convert_opts, &callbacks) == ERR_OK)
                verify_output(ffprobe, opts.output_file, 0, 1, 2, 1, "eng");
            else
                expect(0, "replacement mux preserves rational forced FPS");
            expect(quoted_timing_seen, "complete track/rational-FPS argument is shell quoted");
            snprintf(opts.video_track_file, sizeof(opts.video_track_file),
                     "%s/replacement.mkv", dir);
        }
        {
            char cmd[4096];
            char *quoted = platform_escape_path_for_command(ffmpeg);
            expect(quoted != NULL, "quote subtitle-removal tool");
            if (quoted) {
                snprintf(cmd, sizeof(cmd),
                         "%s -hide_banner -loglevel error -nostdin -y "
                         "-i '%s/source.mkv' -map 0 -map -0:s -c copy "
                         "'%s/source_without_subtitles.mkv'", quoted, dir, dir);
                free(quoted);
                expect(system(cmd) == 0, "create source without subtitles");
                snprintf(opts.intermediate_file, sizeof(opts.intermediate_file),
                         "%s/source_without_subtitles.mkv", dir);
                snprintf(output, sizeof(output), "%s/output_without_subtitles.mov", dir);
                if (mux_run_postprocess_for_preset(&opts, &convert_opts,
                                                  output, NULL) == ERR_OK)
                    verify_output(ffprobe, output, 1, 1, 0, 1, "eng");
                else
                    expect(0, "MOV replacement mux without subtitles succeeds");
            }
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
                    verify_output(ffprobe, opts.output_file, 0, 0, 2, 1, "eng");
                else
                    expect(0, "replacement mux with untitled source succeeds");
            }
            {
                char cmd[4096];
                char *quoted = platform_escape_path_for_command(ffmpeg);
                expect(quoted != NULL, "quote video-title-removal tool");
                if (quoted) {
                    snprintf(cmd, sizeof(cmd),
                             "%s -hide_banner -loglevel error -nostdin -y "
                             "-i '%s/source.mkv' -map 0 -c copy -metadata:s:v:0 title= "
                             "'%s/source_without_video_title.mkv'", quoted, dir, dir);
                    free(quoted);
                    expect(system(cmd) == 0, "create source without video track title");
                    snprintf(opts.intermediate_file, sizeof(opts.intermediate_file),
                             "%s/source_without_video_title.mkv", dir);
                    snprintf(opts.output_file, sizeof(opts.output_file),
                             "%s/output_without_video_title.mkv", dir);
                    if (mux_run_postprocess(&opts, &convert_opts, NULL) == ERR_OK)
                        verify_output(ffprobe, opts.output_file, 0, 1, 2, 0, "eng");
                    else
                        expect(0, "replacement mux with unnamed source video succeeds");
                }
                {
                    char cmd[4096];
                    char *quoted = platform_escape_path_for_command(ffmpeg);
                    expect(quoted != NULL, "quote language-removal tool");
                    if (quoted) {
                        json_t *untagged;
                        const char *language;
                        snprintf(cmd, sizeof(cmd),
                                 "%s -hide_banner -loglevel error -nostdin -y "
                                 "-i '%s/source.mkv' -map 0 -c copy -metadata:s:v:0 language= "
                                 "'%s/source_without_language.mkv'", quoted, dir, dir);
                        free(quoted);
                        expect(system(cmd) == 0, "create source without video language");
                        snprintf(opts.intermediate_file, sizeof(opts.intermediate_file),
                                 "%s/source_without_language.mkv", dir);
                        untagged = probe(ffprobe, opts.intermediate_file);
                        expect(untagged != NULL, "probe untagged source");
                        language = json_string_value(json_object_get(json_object_get(
                            json_array_get(json_object_get(untagged, "streams"), 0), "tags"), "language"));
                        expect(!language || !language[0], "source fixture has no language tag");
                        json_decref(untagged);
                        snprintf(opts.output_file, sizeof(opts.output_file),
                                 "%s/output_without_language.mkv", dir);
                        if (mux_run_postprocess(&opts, &convert_opts, NULL) == ERR_OK)
                            verify_output(ffprobe, opts.output_file, 0, 1, 2, 1, "und");
                        else
                            expect(0, "replacement mux with untagged source video succeeds");
                    }
                }
            }
        }
        {
            char cmd[4096];
            char *quoted = platform_escape_path_for_command(ffmpeg);
            ConverterCallbacks callbacks = {0};
            callbacks.on_message = capture_message;
            expect(quoted != NULL, "quote language-fixture tool");
            if (quoted) {
                snprintf(cmd, sizeof(cmd),
                         "%s -hide_banner -loglevel error -nostdin -y "
                         "-i '%s/source.mkv' -map 0 -c copy "
                         "-metadata:s:v:0 'language=eng; printf LANGUAGE_INJECTION_EXECUTED; #' "
                         "'%s/source_malicious_language.mkv'", quoted, dir, dir);
                free(quoted);
                expect(system(cmd) == 0, "create source with malicious language metadata");
                snprintf(opts.intermediate_file, sizeof(opts.intermediate_file),
                         "%s/source_malicious_language.mkv", dir);
                snprintf(opts.output_file, sizeof(opts.output_file),
                         "%s/output_malicious_language.mkv", dir);
                expect(mux_run_postprocess(&opts, &convert_opts, &callbacks) != ERR_OK,
                       "mkvmerge rejects invalid language as one literal argument");
                expect(!language_injection_seen, "language metadata cannot execute shell commands");
            }
        }
    }
    if (created) {
        const char *configured = getenv("FFPROBE");
        char *saved = configured ? strdup(configured) : NULL;
        expect(!configured || saved != NULL, "save configured ffprobe");
        if (!configured || saved) {
            expect(setenv("FFPROBE", "/nonexistent/ffprobe-metadata-test", 1) == 0,
                   "configure unavailable metadata probe");
            expect(mux_run_postprocess(&opts, &convert_opts, NULL) == ERR_FFPROBE_FAILED,
                   "probe failure is not treated as absent source metadata");
            if (saved)
                setenv("FFPROBE", saved, 1);
            else
                unsetenv("FFPROBE");
            free(saved);
        }
    }
    remove_test_tree(dir);
    expect(access(dir, F_OK) != 0, "remove all media fixtures");
    if (failures)
        return 1;
    puts("replacement-video isolation tests passed");
    return 0;
}
