#include "subtitle_preservation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
static int warnings;

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void message(const char *text)
{
    if (strstr(text, "Warning: subtitle"))
        warnings++;
}

static void test_subtitle(const char *directory, const char *codec,
                           const char *extension, const char *expected)
{
    char input[1024];
    char command[8192];
    char args[2048];
    char actual[128] = "";
    char tiny[4];
    FILE *pipe;
    ConverterCallbacks callbacks = {0};
    callbacks.on_message = message;
    snprintf(input, sizeof(input), "%s/input.%s", directory, extension);
    snprintf(command, sizeof(command),
             "ffmpeg -v error -y -f lavfi -i color=s=64x64:r=1:d=1 "
             "-f lavfi -i anullsrc=r=48000:cl=stereo -i '%s/subtitle.srt' "
             "-map 0:v -map 1:a -map 2:s -c:v mpeg4 -c:a aac -c:s %s "
             "-metadata:s:s:0 language=eng -t 1 '%s'",
             directory, codec, input);
    check(system(command) == 0, "create subtitle fixture");
    check(subtitle_preservation_build(input, MUX_CONTAINER_MKV,
                                      args, sizeof(args), &callbacks),
          "resolve subtitle arguments");
    check(strstr(args, "-map 0:2 -c:s:0 ") != NULL,
          "explicitly map subtitle stream and encoder");
    check(!subtitle_preservation_build(input, MUX_CONTAINER_MKV,
                                       tiny, sizeof(tiny), NULL),
          "reject truncated arguments");
    snprintf(command, sizeof(command),
             "ffmpeg -v error -y -i '%s' -map 0:v -map 0:a -c:v copy -c:a copy "
             "%s '%s/output.mkv'", input, args, directory);
    check(system(command) == 0, "mux compatible subtitles into MKV");
    snprintf(command, sizeof(command),
             "ffprobe -v error -select_streams s -show_entries stream=codec_name "
             "-of csv=p=0 '%s/output.mkv'", directory);
    pipe = popen(command, "r");
    check(pipe != NULL, "probe resulting subtitles");
    if (pipe) {
        check(fgets(actual, sizeof(actual), pipe) != NULL, "subtitle preserved");
        check(pclose(pipe) == 0, "subtitle probe succeeded");
        actual[strcspn(actual, "\r\n")] = '\0';
        check(strcmp(actual, expected) == 0, "expected subtitle codec in final MKV");
    }
    check(subtitle_preservation_build(input, MUX_CONTAINER_MOV,
                                      args, sizeof(args), &callbacks),
          "resolve MOV subtitle arguments");
    snprintf(command, sizeof(command),
             "ffmpeg -v error -y -i '%s' -map 0:v -map 0:a -c:v copy -c:a copy "
             "%s '%s/output.mov'", input, args, directory);
    check(system(command) == 0, "preserve text subtitles when remuxing to MOV");
    unlink(input);
}

static void test_multiple_subtitles(const char *directory)
{
    char input[1024];
    char command[8192];
    char args[2048];
    snprintf(input, sizeof(input), "%s/multiple.mkv", directory);
    snprintf(command, sizeof(command),
             "ffmpeg -v error -y -i '%s/output.mkv' "
             "-map 0:v -map 0:a -map 0:s -map 0:s "
             "-c copy -c:s:0 srt -c:s:1 ass '%s'", directory, input);
    check(system(command) == 0, "create mixed subtitle fixture");
    check(subtitle_preservation_build(input, MUX_CONTAINER_MKV,
                                      args, sizeof(args), NULL),
          "resolve mixed subtitle arguments");
    check(strcmp(args, "-map 0:2 -c:s:0 copy -map 0:3 -c:s:1 copy ") == 0,
          "each subtitle keeps a separate compatible output encoder");
    check(subtitle_preservation_build(input, MUX_CONTAINER_MOV,
                                      args, sizeof(args), NULL),
          "resolve all MOV subtitle encoders");
    check(strcmp(args, "-map 0:2 -c:s:0 mov_text -map 0:3 -c:s:1 mov_text ") == 0,
          "MOV converts each text subtitle individually");
    snprintf(command, sizeof(command),
             "ffmpeg -v error -y -i '%s' -map 0:v -map 0:a -c copy "
             "%s '%s/multiple.mov'", input, args, directory);
    check(system(command) == 0, "remux multiple converted subtitles");
    unlink(input);
    snprintf(input, sizeof(input), "%s/multiple.mov", directory);
    unlink(input);
}

int main(void)
{
    char directory[] = "/tmp/ffmpeg-subtitles-XXXXXX";
    char path[1024];
    char args[256];
    FILE *file;
    if (system("ffmpeg -version >/dev/null 2>&1") != 0 ||
        system("ffprobe -version >/dev/null 2>&1") != 0) {
        puts("SKIP: ffmpeg and ffprobe required");
        return 77;
    }
    setenv("FFPROBE_BIN", "ffprobe", 1);
    if (!mkdtemp(directory))
        return 1;
    snprintf(path, sizeof(path), "%s/subtitle.srt", directory);
    file = fopen(path, "w");
    if (!file)
        return 1;
    fputs("1\n00:00:00,000 --> 00:00:00,800\nPreserved subtitle\n", file);
    fclose(file);
    test_subtitle(directory, "mov_text", "mp4", "subrip");
    test_subtitle(directory, "srt", "mkv", "subrip");
    test_subtitle(directory, "ass", "mkv", "ass");
    test_multiple_subtitles(directory);
    check(warnings >= 3, "subtitle conversions emit warnings");
    check(!subtitle_preservation_build("/nonexistent/subtitle-input", MUX_CONTAINER_MKV,
                                       args, sizeof(args), NULL),
          "probe failure never falls back to blind copy");
    unlink(path);
    snprintf(path, sizeof(path), "%s/output.mkv", directory);
    unlink(path);
    snprintf(path, sizeof(path), "%s/output.mov", directory);
    unlink(path);
    rmdir(directory);
    return failures ? 1 : 0;
}
