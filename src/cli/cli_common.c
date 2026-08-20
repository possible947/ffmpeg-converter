/**
 * cli_common.c
 * Platform-agnostic CLI helpers shared by Linux, macOS, and Windows.
 *
 * This file must not contain any platform #ifdef blocks.
 * All platform-specific behaviour is delegated to cli_platform.h functions.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <stddef.h>

#include "converter.h"
#include "progress.h"
#include "cli_common.h"
#include "cli_platform.h"

/* ---------------------------------------------------------------
 *  CLI Callbacks
 * --------------------------------------------------------------- */

void cli_on_file_begin(const char* filename, int index, int total) {
    progress_end();
    printf("\n[%d/%d] Processing: %s\n", index, total, filename);
}

void cli_on_file_end(const char* filename, ConverterError status) {
    progress_end();
    if (status == ERR_OK)
        printf("Completed: %s\n", filename);
    else
        printf("Error on %s: %s\n", filename, converter_error_string(status));
}

void cli_on_stage(const char* stage) {
    progress_end();
    printf("Stage: %s\n", stage);
}

void cli_on_progress_encode(float percent, float fps, float eta) {
    progress_update(percent, fps, eta);
}

void cli_on_progress_analysis(float percent, float eta) {
    progress_update(percent, 0, eta);
}

void cli_on_message(const char* text) {
    progress_end();
    printf("%s\n", text);
}

void cli_on_error(const char* text, ConverterError code) {
    progress_end();
    printf("ERROR: %s (%s)\n", text, converter_error_string(code));
}

void cli_on_complete(void) {
    progress_end();
    printf("\nAll files processed.\n");
}

/* ---------------------------------------------------------------
 *  Display helpers
 * --------------------------------------------------------------- */

void clear_screen(void) {
    printf("\033[H\033[J");
}

void print_usage(const CliPlatformHandle* h) {
    int i, count;
    const PlatformCodecEntry* entries;

    printf("Usage: ffmpeg_converter [options] file1 file2 ...\n\n");
    printf("Options:\n");

    count   = platform_get_codec_count(h);
    entries = platform_get_codec_entries(h);

    printf("  -c, --codec <");
    for (i = 0; i < count; i++) {
        if (i > 0) printf("|");
        printf("%s", entries[i].name);
    }
    printf(">\n");

    for (i = 0; i < count; i++)
        printf("      %-26s\n", entries[i].name);

    printf("  -p, --profile <lt|standard|hq|4444>\n");
    printf("  -d, --deblock <none|weak|strong>\n");
    printf("  -a, --audio-norm <none|peak|peak2|loudnorm|loudnorm2>\n");
    printf("      --audio-output <pcm|fdk_aac_320|fdk_aac_320_ac3_640>\n");
    if (platform_mux_is_supported())
        printf("      --video-track <file>  replacement video track for mux mode\n");
    printf("  -g, --genre <edm|rock|hiphop|classical|podcast>\n");
    printf("      (genre is used only with loudnorm2)\n");
    printf("  --overwrite        overwrite output files\n");
    printf("  --dry-run          print the conversion plan without running ffmpeg\n");
    printf("  --version          show version and exit\n");
    if (platform_codec_is_available(h, "prores_ks_vulkan"))
        printf("      --vk_device <N>    Vulkan adapter index for prores_ks_vulkan"
               " (default: %d)\n",
               platform_get_default_vulkan_device(h));
    if (platform_codec_is_available(h, "h264_vaapi") ||
        platform_codec_is_available(h, "hevc_vaapi"))
        printf("      --hw_device <path> VAAPI render node for h264_vaapi/hevc_vaapi"
               " (default: auto-detected)\n");
    printf("  -o, --output <directory> set output directory\n");
    printf("  -h, --help         show this help\n\n");
    if (platform_mux_is_supported()) {
        printf("Mux mode:\n");
        printf("  - requires exactly one source file\n");
        printf("  - requires --video-track <file>\n");
        printf("  - runs normal audio processing, then writes final .mkv\n\n");
    }
    if (platform_m4v_is_supported()) {
        printf("Apple M4V options (only used with -c m4v):\n");
        printf("      --m4v-video-track <N>   video stream index (default: 0)\n");
        printf("      --m4v-audio-track <N>   audio stream index (default: 0)\n");
        printf("      --m4v-ac3-bitrate <kbps> AC3 bitrate in kbps (default: 640)\n");
        printf("      --m4v-lang <tag>        audio language tag (default: rus)\n");
        printf("      --m4v-chapters          embed chapter markers (default: on)\n");
        printf("      --no-m4v-chapters       disable chapter markers\n\n");
        printf("Apple M4V mode:\n");
        printf("  - requires MP4Box (GPAC) on PATH\n");
        printf("  - uses libfdk_aac CBR 320k for AAC encoding (fixed)\n");
        printf("  - accepts input with h264, hevc, or prores video\n");
        printf("  - produces dual-audio .m4v (AAC + AC3) compatible with Apple TV\n\n");
    }
    printf("Examples:\n");
    printf("  ffmpeg_converter input.mov\n");
    printf("  ffmpeg_converter -c prores_ks -p hq input.mov\n");
    printf("  ffmpeg_converter -a loudnorm2 -g rock input1.mov input2.mov\n");
    if (platform_m4v_is_supported())
        printf("  ffmpeg_converter -c m4v --m4v-lang eng input.mov\n");
    printf("\n");
}

void print_version(void) {
    printf("ffmpeg_converter %s\n", FFMPEG_CONVERTER_VERSION);
}

void print_summary(const ConvertOptions* opts,
                   const CliM4VOptions* m4v_opts,
                   const char** files, int file_count)
{
    int i;

    printf("\033[1;1H\033[2J");
    printf("\n=== Summary ===\n");
    printf("Codec:        %s\n", opts->codec);

    if (!strcmp(opts->codec, "m4v")) {
        printf("Profile:      (m4v)\n");
        printf("Deblock:      (m4v)\n");
    } else if (!strcmp(opts->codec, "mux")) {
        printf("Profile:      (mux)\n");
        printf("Deblock:      (mux)\n");
    } else if (!strcmp(opts->codec, "prores") ||
               !strcmp(opts->codec, "prores_ks")) {
        const char* profile_str = "none";
        switch (opts->profile) {
            case 1: profile_str = "lt";       break;
            case 2: profile_str = "standard"; break;
            case 3: profile_str = "hq";       break;
            case 4: profile_str = "4444";     break;
        }
        printf("Profile:      %s\n", profile_str);

        const char* deblock_str = "none";
        switch (opts->deblock) {
            case 1: deblock_str = "none";   break;
            case 2: deblock_str = "weak";   break;
            case 3: deblock_str = "strong"; break;
        }
        printf("Deblock:      %s\n", deblock_str);
    } else if (!strcmp(opts->codec, "prores_videotoolbox")) {
        const char* profile_str = "none";
        switch (opts->profile) {
            case 1: profile_str = "lt";       break;
            case 2: profile_str = "standard"; break;
            case 3: profile_str = "hq";       break;
            case 4: profile_str = "4444";     break;
        }
        printf("Profile:      %s\n", profile_str);
        printf("Deblock:      (n/a)\n");
    } else if (!strcmp(opts->codec, "h264_vaapi") ||
               !strcmp(opts->codec, "hevc_vaapi")) {
        printf("Profile:      (n/a)\n");
        printf("Deblock:      (n/a)\n");
        printf("HW device:    %s\n",
               opts->hw_device[0] != '\0' ? opts->hw_device : "(auto)");
    } else {
        printf("Profile:      (n/a)\n");
        printf("Deblock:      (n/a)\n");
    }

    printf("Audio norm:   %s\n", opts->audio_norm);
    printf("Audio out:    %s\n",
           opts->audio_output_mode[0] != '\0' ? opts->audio_output_mode : "pcm");

    if (!strcmp(opts->codec, "mux"))
        printf("Video track:  %s\n",
               opts->video_track_path[0] != '\0'
                   ? opts->video_track_path : "(missing)");

    if (!strcmp(opts->codec, "m4v") && m4v_opts) {
        printf("M4V video idx:%d\n", m4v_opts->video_track_index);
        printf("M4V audio idx:%d\n", m4v_opts->audio_track_index);
        printf("M4V AAC:      CBR 320k (libfdk_aac)\n");
        printf("M4V AC3 kbps: %d\n", m4v_opts->ac3_bitrate_kbps);
        printf("M4V lang:     %s\n", m4v_opts->audio_lang[0] != '\0'
                                        ? m4v_opts->audio_lang : "rus");
        printf("M4V chapters: %s\n", m4v_opts->add_chapters ? "yes" : "no");
    }

    if (!strcmp(opts->audio_norm, "loudness_norm_2pass")) {
        const char* genre_str = "none";
        switch (opts->genre) {
            case 1: genre_str = "edm";       break;
            case 2: genre_str = "rock";      break;
            case 3: genre_str = "hiphop";    break;
            case 4: genre_str = "classical"; break;
            case 5: genre_str = "podcast";   break;
        }
        printf("Genre:        %s\n", genre_str);
    }

    printf("Overwrite:    %s\n", opts->overwrite ? "yes" : "no");
    printf("Dry run:      %s\n", opts->dry_run ? "yes" : "no");
    if (opts->output_dir[0] != '\0')
        printf("Output dir:   %s\n", opts->output_dir);
    else
        printf("Output dir:   (same as input)\n");

    if (opts->output_dir[0] != '\0') {
        if (opts->output_dir_status)
            printf("Dir status:   OK\n");
        else
            printf("Dir status:   ERROR (directory missing or not writable)\n");
    }

    printf("\nFiles (%d):\n", file_count);
    for (i = 0; i < file_count; ++i) {
        if (strchr(files[i], ' ') != NULL)
            printf("  \"%s\"\n", files[i]);
        else
            printf("  %s\n", files[i]);
    }
    printf("===============\n");
}

/* ---------------------------------------------------------------
 *  Input helpers
 * --------------------------------------------------------------- */

int read_choice(void) {
    char buf[64];
    char* p;

    if (!fgets(buf, sizeof(buf), stdin))
        return EOF;

    for (p = buf; *p != '\0'; ++p) {
        if (*p == '\n')
            return '\n';
        if (!isspace((unsigned char)*p))
            return *p;
    }
    return '\n';
}

int process_input_path(const char* input, char* output, size_t out_size) {
    char* temp;
    char* start;
    char* end;
    size_t len, final_len;
    int j = 0;
    int in_quotes = 0;
    char quote_char = 0;
    int escape_next = 0;
    size_t i;

    if (!input || !output || out_size == 0)
        return 0;

    len = strlen(input);
    if (len == 0) {
        output[0] = '\0';
        return 1;
    }

    temp = malloc(len + 1);
    if (!temp)
        return 0;

    for (i = 0; i < len; i++) {
        if (escape_next) {
            temp[j++] = input[i];
            escape_next = 0;
            continue;
        }

        if (input[i] == '\\') {
            if (i + 1 < len &&
                (input[i + 1] == ' ' || input[i + 1] == '\\' ||
                 input[i + 1] == '\'' || input[i + 1] == '"')) {
                escape_next = 1;
                continue;
            }
            temp[j++] = input[i];
            continue;
        }

        if (!in_quotes && (input[i] == '\'' || input[i] == '"')) {
            in_quotes = 1;
            quote_char = input[i];
            continue;
        }

        if (in_quotes && input[i] == quote_char) {
            in_quotes = 0;
            continue;
        }

        temp[j++] = input[i];
    }

    temp[j] = '\0';

    start = temp;
    end   = temp + j - 1;

    while (start <= end && isspace((unsigned char)*start))
        start++;
    while (end > start && isspace((unsigned char)*end))
        end--;

    final_len = (size_t)(end - start + 1);

    if (final_len == 0) {
        free(temp);
        return 0;
    }

    if (final_len < out_size) {
        strncpy(output, start, final_len);
        output[final_len] = '\0';
    } else {
        strncpy(output, start, out_size - 1);
        output[out_size - 1] = '\0';
    }

    free(temp);
    return 1;
}

int read_input_list(char*** out_files, int max_cnt, int* count) {
    char line[1024];
    char** files;
    int idx = 0;

    files = malloc(sizeof(char*) * (size_t)max_cnt);
    if (!files)
        return -1;

    printf("Enter file names (you can drag & drop files). "
           "Finish with empty line:\n");

    while (idx < max_cnt) {
        char processed_path[1024];

        printf("File %d: ", idx + 1);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
            break;

        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0')
            break;

        if (strcmp(line, "c") == 0 || strcmp(line, "C") == 0) {
            int k;
            for (k = 0; k < idx; k++) free(files[k]);
            free(files);
            return -1;
        }

        /* Detect drag-and-drop of multiple files: the shell quotes each path
         * and separates them with spaces — e.g. "path1" "path2" "path3".
         * Count how many quoted tokens are present on this line. */
        {
            const char* p = line;
            int token_count = 0;
            while (*p) {
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '"') {
                    const char* end = strchr(p + 1, '"');
                    if (!end) break;
                    token_count++;
                    p = end + 1;
                } else if (*p != '\0') {
                    token_count++;
                    break; /* unquoted — single token */
                }
            }

            if (token_count > 1) {
                /* Multi-path line: extract each quoted segment */
                p = line;
                while (*p && idx < max_cnt) {
                    char single_path[1024];
                    const char* end;
                    size_t path_len;

                    while (*p == ' ' || *p == '\t') p++;
                    if (*p != '"') break;

                    end = strchr(p + 1, '"');
                    if (!end) break;

                    path_len = (size_t)(end - (p + 1));
                    if (path_len >= sizeof(single_path))
                        path_len = sizeof(single_path) - 1;
                    strncpy(single_path, p + 1, path_len);
                    single_path[path_len] = '\0';
                    p = end + 1;

                    if (single_path[0] == '\0') continue;

                    if (!platform_file_is_regular_readable(single_path)) {
                        printf("File not found or not readable: %s\n", single_path);
                        continue;
                    }
                    {
                        char* fname = strdup(single_path);
                        if (!fname) {
                            int k;
                            for (k = 0; k < idx; k++) free(files[k]);
                            free(files);
                            return -1;
                        }
                        files[idx++] = fname;
                    }
                    printf("Added: %s\n", single_path);
                }
                continue; /* prompt for next file */
            }
        }

        /* Single path: strip shell quoting via process_input_path */
        if (!process_input_path(line, processed_path, sizeof(processed_path))) {
            printf("Error processing path\n");
            continue;
        }

        if (!platform_file_is_regular_readable(processed_path)) {
            printf("File not found or not readable: %s\n", processed_path);
            continue;
        }

        {
            char* fname = strdup(processed_path);
            if (!fname) {
                int k;
                for (k = 0; k < idx; k++) free(files[k]);
                free(files);
                return -1;
            }
            files[idx++] = fname;
        }
        printf("Added: %s\n", processed_path);
    }

    *out_files = files;
    *count     = idx;

    if (idx > 0)
        printf("\nSuccessfully added %d file(s)\n", idx);

    return 0;
}

int read_output_dir(char* out_buf, size_t bufsize, int* status) {
    char tmp[CLI_BUFFER_SIZE];
    const char* home;

    printf("output directory (default: HOME/ffmpeg_converter):\n> ");
    if (!fgets(tmp, sizeof(tmp), stdin))
        return -1;

    tmp[strcspn(tmp, "\r\n")] = '\0';

    if (tmp[0] == '\0') {
        home = cli_get_home_dir();
        snprintf(tmp, sizeof(tmp), "%s/ffmpeg_converter", home);
    }

    if (!platform_ensure_output_dir(tmp)) {
        fprintf(stderr, "Error: cannot create or access output directory: %s\n", tmp);
        *status = 0;
        return -1;
    }

    *status = 1;
    strncpy(out_buf, tmp, bufsize - 1);
    out_buf[bufsize - 1] = '\0';
    return 0;
}

int read_single_file_path(const char* prompt,
                          char* out_path, size_t out_path_sz)
{
    char line[1024];

    printf("%s\n> ", prompt);
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin))
        return -1;

    line[strcspn(line, "\r\n")] = '\0';
    if (line[0] == '\0' ||
        strcmp(line, "c") == 0 ||
        strcmp(line, "C") == 0)
        return -1;

    if (!process_input_path(line, out_path, out_path_sz))
        return -1;

    if (!platform_file_is_regular_readable(out_path)) {
        printf("Invalid file: %s\n", out_path);
        return -1;
    }

    return 0;
}

/* ---------------------------------------------------------------
 *  File validation
 * --------------------------------------------------------------- */

int verify_all_files(const char** files, int file_count) {
    int  i, j, valid_files = 0;
    int* ok;

    printf("\nVerifying files...\n");

    ok = (int*)malloc(sizeof(int) * (size_t)file_count);
    if (!ok) {
        fprintf(stderr, "Out of memory\n");
        return 0;
    }

    for (i = 0; i < file_count; i++) {
        ok[i] = platform_file_is_regular_readable(files[i]);
        printf("  %s: %s\n", ok[i] ? "OK" : "FAIL", files[i]);
        if (ok[i]) valid_files++;
    }

    printf("\nFound %d valid file(s) out of %d\n", valid_files, file_count);

    if (valid_files == 0) {
        printf("No valid files to process.\n");
        free(ok);
        return 0;
    }

    if (valid_files < file_count) {
        int ch;
        printf("Continue with %d file(s)? [y/N]: ", valid_files);
        ch = getchar();
        while (getchar() != '\n')
            ;
        if (ch != 'y' && ch != 'Y') {
            free(ok);
            return 0;
        }
        /* Compact: move valid entries to the front of the array */
        j = 0;
        for (i = 0; i < file_count; i++) {
            if (ok[i])
                files[j++] = files[i];
        }
    }

    free(ok);
    return valid_files;
}

/* ---------------------------------------------------------------
 *  Argument parsing
 * --------------------------------------------------------------- */

int parse_args(int argc, char** argv, const CliPlatformHandle* h,
               ConvertOptions* opts, CliM4VOptions* m4v_opts,
               const char** files, int* file_count)
{
    int i;

    /* Zero the struct so every field (hw_device, vulkan_device, gain,
     * measured_*, ...) has a deterministic value before defaults are set. */
    memset(opts, 0, sizeof(*opts));

    strcpy(opts->codec, "prores_ks");
    opts->profile   = 2;  /* standard */
    opts->deblock   = 1;  /* none */
    strcpy(opts->audio_norm, "peak_norm_2pass");
    strcpy(opts->audio_output_mode, "pcm");
    opts->genre     = 1;  /* edm */
    opts->overwrite = 0;
    opts->output_dir[0] = '\0';
    opts->output_dir_status = 0;
    opts->video_track_path[0] = '\0';
    opts->vulkan_device = platform_get_default_vulkan_device(h);

    /* M4V defaults */
    if (m4v_opts) {
        m4v_opts->video_track_index = 0;
        m4v_opts->audio_track_index = 0;
        m4v_opts->ac3_bitrate_kbps  = 640;
        strcpy(m4v_opts->audio_lang, "rus");
        m4v_opts->add_chapters      = 1;
    }

    *file_count = 0;

    for (i = 1; i < argc; i++) {

        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_usage(h);
            return 0;
        }

        if (!strcmp(argv[i], "--codec") || !strcmp(argv[i], "-c")) {
            if (i + 1 >= argc) return 0;
            i++;
            if (!platform_codec_is_available(h, argv[i]))
                return 0;
            strncpy(opts->codec, argv[i], sizeof(opts->codec) - 1);
            opts->codec[sizeof(opts->codec) - 1] = '\0';
            continue;
        }

        if (!strcmp(argv[i], "--profile") || !strcmp(argv[i], "-p")) {
            if (i + 1 >= argc) return 0;
            i++;
            if      (!strcmp(argv[i], "lt"))       opts->profile = 1;
            else if (!strcmp(argv[i], "standard")) opts->profile = 2;
            else if (!strcmp(argv[i], "hq"))       opts->profile = 3;
            else if (!strcmp(argv[i], "4444"))     opts->profile = 4;
            else return 0;
            continue;
        }

        if (!strcmp(argv[i], "--deblock") || !strcmp(argv[i], "-d")) {
            if (i + 1 >= argc) return 0;
            i++;
            if      (!strcmp(argv[i], "none"))   opts->deblock = 1;
            else if (!strcmp(argv[i], "weak"))   opts->deblock = 2;
            else if (!strcmp(argv[i], "strong")) opts->deblock = 3;
            else return 0;
            continue;
        }

        if (!strcmp(argv[i], "--audio-norm") || !strcmp(argv[i], "-a")) {
            if (i + 1 >= argc) return 0;
            i++;
            if      (!strcmp(argv[i], "none"))      strcpy(opts->audio_norm, "none");
            else if (!strcmp(argv[i], "peak"))      strcpy(opts->audio_norm, "peak_norm");
            else if (!strcmp(argv[i], "peak2"))     strcpy(opts->audio_norm, "peak_norm_2pass");
            else if (!strcmp(argv[i], "loudnorm"))  strcpy(opts->audio_norm, "loudness_norm");
            else if (!strcmp(argv[i], "loudnorm2")) strcpy(opts->audio_norm, "loudness_norm_2pass");
            else return 0;
            continue;
        }

        if (!strcmp(argv[i], "--audio-output")) {
            if (i + 1 >= argc) return 0;
            i++;
            if (!platform_audio_mode_is_available(argv[i]))
                return 0;
            strncpy(opts->audio_output_mode, argv[i],
                    sizeof(opts->audio_output_mode) - 1);
            opts->audio_output_mode[sizeof(opts->audio_output_mode) - 1] = '\0';
            continue;
        }

        if (!strcmp(argv[i], "--video-track")) {
            if (!platform_mux_is_supported()) return 0;
            if (i + 1 >= argc) return 0;
            i++;
            strncpy(opts->video_track_path, argv[i],
                    sizeof(opts->video_track_path) - 1);
            opts->video_track_path[sizeof(opts->video_track_path) - 1] = '\0';
            continue;
        }

        if (!strcmp(argv[i], "--genre") || !strcmp(argv[i], "-g")) {
            if (i + 1 >= argc) return 0;
            i++;
            if      (!strcmp(argv[i], "edm"))       opts->genre = 1;
            else if (!strcmp(argv[i], "rock"))      opts->genre = 2;
            else if (!strcmp(argv[i], "hiphop"))    opts->genre = 3;
            else if (!strcmp(argv[i], "classical")) opts->genre = 4;
            else if (!strcmp(argv[i], "podcast"))   opts->genre = 5;
            else return 0;
            continue;
        }

        if (!strcmp(argv[i], "--overwrite")) {
            opts->overwrite = 1;
            continue;
        }

        if (!strcmp(argv[i], "--dry-run")) {
            opts->dry_run = 1;
            continue;
        }

        if (!strcmp(argv[i], "--m4v-video-track")) {
            if (i + 1 >= argc) return 0;
            i++;
            if (m4v_opts) m4v_opts->video_track_index = atoi(argv[i]);
            continue;
        }

        if (!strcmp(argv[i], "--m4v-audio-track")) {
            if (i + 1 >= argc) return 0;
            i++;
            if (m4v_opts) m4v_opts->audio_track_index = atoi(argv[i]);
            continue;
        }

        if (!strcmp(argv[i], "--m4v-ac3-bitrate")) {
            int b;
            if (i + 1 >= argc) return 0;
            i++;
            b = atoi(argv[i]);
            if (b <= 0) return 0;
            if (m4v_opts) m4v_opts->ac3_bitrate_kbps = b;
            continue;
        }

        if (!strcmp(argv[i], "--m4v-lang")) {
            if (i + 1 >= argc) return 0;
            i++;
            if (m4v_opts) {
                strncpy(m4v_opts->audio_lang, argv[i],
                        sizeof(m4v_opts->audio_lang) - 1);
                m4v_opts->audio_lang[sizeof(m4v_opts->audio_lang) - 1] = '\0';
            }
            continue;
        }

        if (!strcmp(argv[i], "--m4v-chapters")) {
            if (m4v_opts) m4v_opts->add_chapters = 1;
            continue;
        }

        if (!strcmp(argv[i], "--no-m4v-chapters")) {
            if (m4v_opts) m4v_opts->add_chapters = 0;
            continue;
        }

        if (!strcmp(argv[i], "--vk_device")) {
            if (i + 1 >= argc) return 0;
            i++;
            {
                char *endptr;
                long val = strtol(argv[i], &endptr, 10);
                if (*endptr != '\0' || val < 0 || val > 7) return 0;
                opts->vulkan_device = (int)val;
            }
            continue;
        }

        if (!strcmp(argv[i], "--hw_device")) {
            if (i + 1 >= argc) return 0;
            i++;
            if (argv[i][0] == '\0') return 0;
            strncpy(opts->hw_device, argv[i], sizeof(opts->hw_device) - 1);
            opts->hw_device[sizeof(opts->hw_device) - 1] = '\0';
            continue;
        }

        if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
            if (i + 1 >= argc) return 0;
            i++;
            strncpy(opts->output_dir, argv[i], sizeof(opts->output_dir) - 1);
            opts->output_dir[sizeof(opts->output_dir) - 1] = '\0';
            if (platform_dir_is_writable(opts->output_dir)) {
                opts->output_dir_status = 1;
            } else {
                opts->output_dir_status = 0;
                fprintf(stderr,
                        "Warning: output directory not writable or missing: %s\n",
                        opts->output_dir);
            }
            continue;
        }

        if (argv[i][0] != '-') {
            files[*file_count] = argv[i];
            (*file_count)++;
            continue;
        }

        return 0;
    }

    return 1;
}

/* ---------------------------------------------------------------
 *  Interactive menu
 *
 *  Steps:
 *   1  – codec selection
 *   2  – profile selection   (only if codec needs_profile)
 *   3  – deblock selection   (only if codec needs_deblock)
 *   4  – audio normalization
 *   5  – genre               (only if loudnorm2 selected)
 *   6  – audio output mode
 *   7  – overwrite
 *   8  – output directory
 *   9  – input file list
 *   10 – mux video-track     (only if mux codec and platform_mux_is_supported())
 *   11 – finalize
 *   12 – done (exit loop)
 * --------------------------------------------------------------- */

static void free_temp_files(char** files, int count) {
    int i;
    if (!files) return;
    for (i = 0; i < count; i++) free(files[i]);
    free(files);
}

int run_menu(const CliPlatformHandle* h, ConvertOptions* opts,
             CliM4VOptions* m4v_opts,
             const char*** files_ptr, int* file_count)
{
    int step        = 1;
    int codec_idx   = 0;   /* index into platform codec entries */
    int profile     = 2;   /* standard */
    int deblock     = 1;   /* none */
    int audio_norm  = 3;   /* peak 2-pass */
    int audio_output= 1;   /* pcm */
    int genre       = 1;   /* edm */
    int overwrite   = 0;
    char output_dir[CLI_BUFFER_SIZE];
    int output_dir_status = 0;
    char video_track_path[CLI_BUFFER_SIZE];

    /* M4V interactive state */
    int  m4v_ac3_bitrate_kbps = 640;
    char m4v_audio_lang[16];
    int  m4v_add_chapters     = 1;
    int  m4v_video_track      = 0;
    int  m4v_audio_track      = 0;

    char** temp_files      = NULL;
    int    temp_file_count = 0;
    int    result          = -1;

    int codec_count = platform_get_codec_count(h);
    const PlatformCodecEntry* entries = platform_get_codec_entries(h);

    /* Zero the option structs so hw_device / vulkan_device and other fields
     * that are not filled by the menu have deterministic values. */
    memset(opts, 0, sizeof(*opts));
    if (m4v_opts)
        memset(m4v_opts, 0, sizeof(*m4v_opts));

    output_dir[0]        = '\0';
    video_track_path[0]  = '\0';
    strcpy(m4v_audio_lang, "rus");

    while (step != 12 && step != 0) {
        switch (step) {

        /* ---- Step 1: codec ---- */
        case 1: {
            int k, ch;
            clear_screen();
            printf("----ffmpeg_converter_simple_gui----\n\n");
            printf("select codec\n");
            printf("----------------------\n");
            for (k = 0; k < codec_count; k++)
                printf("  %d. %s%s\n", k + 1, entries[k].name,
                       k == 0 ? " (default)" : "");
            printf("----------------------\n");
            printf("select: number->choice,Enter->(default),c->cancel\n>");
            ch = read_choice();
            if (ch == '\n') {
                codec_idx = 0;
                step = entries[0].needs_profile ? 2
                     : (!strcmp(entries[0].name, "m4v") ? 7 : 4);
            } else if (ch >= '1' && ch < '1' + codec_count) {
                codec_idx = ch - '1';
                step = entries[codec_idx].needs_profile ? 2
                     : (!strcmp(entries[codec_idx].name, "m4v") ? 7 : 4);
            } else if (ch == 'c' || ch == 'C') {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            } else {
                printf("Invalid choice\n");
            }
            break;
        }

        /* ---- Step 2: profile ---- */
        case 2: {
            int ch;
            clear_screen();
            printf("----ffmpeg_converter_simple_gui----\n\n");
            printf("select profile\n");
            printf("-----------------------\n");
            printf("  1. lt\n");
            printf("  2. standard (default)\n");
            printf("  3. hq\n");
            printf("-----------------------\n");
            printf("select: number->choice,Enter->(default),c->cancel,b->back\n>");
            ch = read_choice();
            {
                int next = entries[codec_idx].needs_deblock ? 3 : 4;
                if      (ch == '\n') { profile = 2; step = next; }
                else if (ch == '1') { profile = 1; step = next; }
                else if (ch == '2') { profile = 2; step = next; }
                else if (ch == '3') { profile = 3; step = next; }
                else if (ch == 'c' || ch == 'C') {
                    free_temp_files(temp_files, temp_file_count);
                    return -1;
                } else if (ch == 'b' || ch == 'B') {
                    step = 1;
                } else {
                    printf("Invalid choice\n");
                }
            }
            break;
        }

        /* ---- Step 3: deblock ---- */
        case 3: {
            int ch;
            clear_screen();
            printf("----ffmpeg_converter_simple_gui----\n\n");
            printf("select deblock\n");
            printf("---------------------------\n");
            printf("  1. none (default)\n");
            printf("  2. weak (4K content)\n");
            printf("  3. strong (1080p content)\n");
            printf("---------------------------\n");
            printf("select: number->choice,Enter->(default),c->cancel,b->back\n>");
            ch = read_choice();
            if      (ch == '\n') { deblock = 1; step = 4; }
            else if (ch == '1') { deblock = 1; step = 4; }
            else if (ch == '2') { deblock = 2; step = 4; }
            else if (ch == '3') { deblock = 3; step = 4; }
            else if (ch == 'c' || ch == 'C') {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            } else if (ch == 'b' || ch == 'B') {
                step = 2;
            } else {
                printf("Invalid choice\n");
            }
            break;
        }

        /* ---- Step 4: audio normalization ---- */
        case 4: {
            int ch;
            int prev = entries[codec_idx].needs_deblock ? 3
                     : entries[codec_idx].needs_profile ? 2 : 1;
            clear_screen();
            printf("----ffmpeg_converter_simple_gui----\n\n");
            printf("select audio normalization\n");
            printf("---------------------------------\n");
            printf("  1. none\n");
            printf("  2. peak\n");
            printf("  3. peak 2-pass (default)\n");
            printf("  4. loudness normalization\n");
            printf("  5. loudness normalization 2-pass\n");
            printf("---------------------------------\n");
            printf("select: number->choice,Enter->(default),c->cancel,b->back\n>");
            ch = read_choice();
            if      (ch == '\n') { audio_norm = 3; step = 6; }
            else if (ch == '1') { audio_norm = 1; step = 6; }
            else if (ch == '2') { audio_norm = 2; step = 6; }
            else if (ch == '3') { audio_norm = 3; step = 6; }
            else if (ch == '4') { audio_norm = 4; step = 6; }
            else if (ch == '5') { audio_norm = 5; step = 5; }
            else if (ch == 'c' || ch == 'C') {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            } else if (ch == 'b' || ch == 'B') {
                step = prev;
            } else {
                printf("Invalid choice\n");
            }
            break;
        }

        /* ---- Step 5: genre ---- */
        case 5: {
            int ch;
            clear_screen();
            printf("----ffmpeg_converter_simple_gui----\n\n");
            printf("select audio normalization genre\n");
            printf("---------------------------------\n");
            printf("  1. EDM (default)\n");
            printf("  2. Rock\n");
            printf("  3. HipHop\n");
            printf("  4. Classical\n");
            printf("  5. Podcast\n");
            printf("---------------------------------\n");
            printf("select: number->choice,Enter->(default),c->cancel,b->back\n>");
            ch = read_choice();
            if      (ch == '\n') { genre = 1; step = 6; }
            else if (ch == '1') { genre = 1; step = 6; }
            else if (ch == '2') { genre = 2; step = 6; }
            else if (ch == '3') { genre = 3; step = 6; }
            else if (ch == '4') { genre = 4; step = 6; }
            else if (ch == '5') { genre = 5; step = 6; }
            else if (ch == 'c' || ch == 'C') {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            } else if (ch == 'b' || ch == 'B') {
                step = 4;
            } else {
                printf("Invalid choice\n");
            }
            break;
        }

        /* ---- Step 6: audio output ---- */
        case 6: {
            int ch;
            clear_screen();
            printf("----ffmpeg_converter_simple_gui----\n\n");
            printf("select audio output\n");
            printf("----------------------------------\n");
            printf("  1. pcm (default)\n");
            printf("  2. fdk_aac_320\n");
            printf("  3. fdk_aac_320_ac3_640\n");
            printf("----------------------------------\n");
            printf("select: number->choice,Enter->(default),c->cancel,b->back\n>");
            ch = read_choice();
            {
                int prev = (audio_norm == 5) ? 5 : 4;
                if      (ch == '\n') { audio_output = 1; step = 7; }
                else if (ch == '1') { audio_output = 1; step = 7; }
                else if (ch == '2') { audio_output = 2; step = 7; }
                else if (ch == '3') { audio_output = 3; step = 7; }
                else if (ch == 'c' || ch == 'C') {
                    free_temp_files(temp_files, temp_file_count);
                    return -1;
                } else if (ch == 'b' || ch == 'B') {
                    step = prev;
                } else {
                    printf("Invalid choice\n");
                }
            }
            break;
        }

        /* ---- Step 7: overwrite ---- */
        case 7: {
            int ch;
            int prev = (!strcmp(entries[codec_idx].name, "m4v")) ? 1
                     : 6;
            printf("\nchoice if overwrite files: yes/No\n");
            printf("select:y/n,Enter->(default),c->cancel,b->back\n>");
            ch = read_choice();
            if      (ch == '\n') { overwrite = 0; step = 8; }
            else if (ch == 'y' || ch == 'Y') { overwrite = 1; step = 8; }
            else if (ch == 'n' || ch == 'N') { overwrite = 0; step = 8; }
            else if (ch == 'c' || ch == 'C') {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            } else if (ch == 'b' || ch == 'B') {
                step = prev;
            } else {
                printf("Invalid choice\n");
            }
            break;
        }

        /* ---- Step 8: output directory ---- */
        case 8: {
            clear_screen();
            printf("----ffmpeg_converter_simple_gui----\n\n");
            if (read_output_dir(output_dir, sizeof(output_dir),
                                &output_dir_status) == 0) {
                step = 9;
            } else {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            }
            break;
        }

        /* ---- Step 9: input files ---- */
        case 9: {
            const int MAX_FILES = 128;
            int file_cnt = 0;
            char** file_list = NULL;

            clear_screen();
            printf("----ffmpeg_converter_simple_gui----\n\n");
            if (read_input_list(&file_list, MAX_FILES, &file_cnt) != 0) {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            }

            /* mux requires exactly one source file */
            if (platform_mux_is_supported() &&
                !strcmp(entries[codec_idx].name, "mux") &&
                file_cnt != 1) {
                printf("Mux mode requires exactly one source file.\n");
                free_temp_files(file_list, file_cnt);
                break;
            }

            free_temp_files(temp_files, temp_file_count);
            temp_files      = file_list;
            temp_file_count = file_cnt;

            /* mux codec → collect video track */
            if (platform_mux_is_supported() &&
                !strcmp(entries[codec_idx].name, "mux"))
                step = 10;
            /* m4v codec → collect m4v-specific options */
             else if (platform_m4v_is_supported() &&
                      !strcmp(entries[codec_idx].name, "m4v"))
                 step = 14;
             else
                 step = 11;
            break;
        }

        /* ---- Step 10: mux video track ---- */
        case 10: {
            clear_screen();
            printf("----ffmpeg_converter_simple_gui----\n\n");
            if (read_single_file_path("video-track file for mux mode:",
                                      video_track_path,
                                      sizeof(video_track_path)) == 0) {
                step = 11;
            } else {
                printf("Invalid video-track file\n");
                step = 0;
            }
            break;
        }

        /* ---- Step 11: finalize ---- */
        case 11: {
            strncpy(opts->codec, entries[codec_idx].name,
                    sizeof(opts->codec) - 1);
            opts->codec[sizeof(opts->codec) - 1] = '\0';

            opts->profile = profile;
            opts->deblock = deblock;

            switch (audio_norm) {
                case 1: strcpy(opts->audio_norm, "none");                break;
                case 2: strcpy(opts->audio_norm, "peak_norm");           break;
                case 3: strcpy(opts->audio_norm, "peak_norm_2pass");     break;
                case 4: strcpy(opts->audio_norm, "loudness_norm");       break;
                case 5: strcpy(opts->audio_norm, "loudness_norm_2pass"); break;
                default: strcpy(opts->audio_norm, "peak_norm_2pass");    break;
            }

            switch (audio_output) {
                case 1: strcpy(opts->audio_output_mode, "pcm");                  break;
                case 2: strcpy(opts->audio_output_mode, "fdk_aac_320");           break;
                case 3: strcpy(opts->audio_output_mode, "fdk_aac_320_ac3_640");   break;
                default: strcpy(opts->audio_output_mode, "pcm");                  break;
            }

            opts->genre     = genre;
            opts->overwrite = overwrite;

            strncpy(opts->output_dir, output_dir,
                    sizeof(opts->output_dir) - 1);
            opts->output_dir[sizeof(opts->output_dir) - 1] = '\0';
            opts->output_dir_status = output_dir_status;

            if (platform_mux_is_supported() &&
                !strcmp(opts->codec, "mux")) {
                strncpy(opts->video_track_path, video_track_path,
                        sizeof(opts->video_track_path) - 1);
                opts->video_track_path[sizeof(opts->video_track_path) - 1] = '\0';
            }

            /* Copy collected M4V options */
            if (m4v_opts && !strcmp(opts->codec, "m4v")) {
                m4v_opts->video_track_index = m4v_video_track;
                m4v_opts->audio_track_index = m4v_audio_track;
                m4v_opts->ac3_bitrate_kbps  = m4v_ac3_bitrate_kbps;
                strncpy(m4v_opts->audio_lang, m4v_audio_lang,
                        sizeof(m4v_opts->audio_lang) - 1);
                m4v_opts->audio_lang[sizeof(m4v_opts->audio_lang) - 1] = '\0';
                m4v_opts->add_chapters = m4v_add_chapters;
            }

            platform_apply_hw_device(opts, h);

            *files_ptr  = (const char**)temp_files;
            *file_count = temp_file_count;

            result = 0;
            step   = 12;
            break;
        }

        /* ---- Step 12: done (loop exit) ---- */

        case 14: {
            int ch;
            clear_screen();
            printf("----ffmpeg_converter_simple_gui----\n\n");
            printf("Apple M4V: AC3 audio bitrate\n");
            printf("------------------------------------------\n");
            printf("  1. 384 kbps\n");
            printf("  2. 448 kbps\n");
            printf("  3. 640 kbps (default)\n");
            printf("------------------------------------------\n");
            printf("select: number->choice,Enter->(default),c->cancel,b->back\n>");
            ch = read_choice();
            if      (ch == '\n') { m4v_ac3_bitrate_kbps = 640; step = 15; }
            else if (ch == '1')  { m4v_ac3_bitrate_kbps = 384; step = 15; }
            else if (ch == '2')  { m4v_ac3_bitrate_kbps = 448; step = 15; }
            else if (ch == '3')  { m4v_ac3_bitrate_kbps = 640; step = 15; }
            else if (ch == 'c' || ch == 'C') {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            }             else if (ch == 'b' || ch == 'B') {
                step = 12;
            } else {
                printf("Invalid choice\n");
            }
            break;
        }

        /* ---- Step 15: M4V — audio language ---- */
        case 15: {
            char line[32];
            clear_screen();
            printf("----ffmpeg_converter_simple_gui----\n\n");
            printf("Apple M4V: audio language tag\n");
            printf("  Examples: rus, eng, deu, fra, spa\n");
            printf("  Press Enter for default (rus), c to cancel, b to go back\n>");
            fflush(stdout);
            if (!fgets(line, sizeof(line), stdin)) {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            }
            line[strcspn(line, "\r\n")] = '\0';
            if (strcmp(line, "c") == 0 || strcmp(line, "C") == 0) {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            } else if (strcmp(line, "b") == 0 || strcmp(line, "B") == 0) {
                step = 14;
            } else {
                if (line[0] != '\0') {
                    strncpy(m4v_audio_lang, line, sizeof(m4v_audio_lang) - 1);
                    m4v_audio_lang[sizeof(m4v_audio_lang) - 1] = '\0';
                }
                step = 16;
            }
            break;
        }

        /* ---- Step 16: M4V — chapters + track indices ---- */
        case 16: {
            char line[32];
            clear_screen();
            printf("----ffmpeg_converter_simple_gui----\n\n");
            printf("Apple M4V: embed chapter markers?\n");
            printf("  y/Enter=yes (default), n=no, c=cancel, b=back\n>");
            fflush(stdout);
            if (!fgets(line, sizeof(line), stdin)) {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            }
            line[strcspn(line, "\r\n")] = '\0';
            if (strcmp(line, "c") == 0 || strcmp(line, "C") == 0) {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            } else if (strcmp(line, "b") == 0 || strcmp(line, "B") == 0) {
                step = 15;
            } else {
                m4v_add_chapters = (line[0] == 'n' || line[0] == 'N') ? 0 : 1;
                step = 17;
            }
            break;
        }

        /* ---- Step 17: M4V — stream track indices ---- */
        case 17: {
            char line[32];
            clear_screen();
            printf("----ffmpeg_converter_simple_gui----\n\n");
            printf("Apple M4V: video stream index (0-based, default 0)\n>");
            fflush(stdout);
            if (!fgets(line, sizeof(line), stdin)) {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            }
            line[strcspn(line, "\r\n")] = '\0';
            if (strcmp(line, "c") == 0 || strcmp(line, "C") == 0) {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            } else if (strcmp(line, "b") == 0 || strcmp(line, "B") == 0) {
                step = 16;
                break;
            } else {
                if (line[0] != '\0') m4v_video_track = atoi(line);
            }

            printf("Apple M4V: audio stream index (0-based, default 0)\n>");
            fflush(stdout);
            if (!fgets(line, sizeof(line), stdin)) {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            }
            line[strcspn(line, "\r\n")] = '\0';
            if (strcmp(line, "c") == 0 || strcmp(line, "C") == 0) {
                free_temp_files(temp_files, temp_file_count);
                return -1;
            }
            if (line[0] != '\0') m4v_audio_track = atoi(line);

            step = 11;
            break;
        }

        } /* switch */
    } /* while */

    if (result < 0)
        free_temp_files(temp_files, temp_file_count);

    return result;
}
