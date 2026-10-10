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
#include "preset_loader.h"
#include <jansson.h>

static const char* get_platform_name(void);

static int codec_uses_vaapi_device(const char* codec) {
    return codec &&
           (strcmp(codec, "h264_vaapi") == 0 ||
            strcmp(codec, "hevc_vaapi") == 0 ||
            strcmp(codec, "av1_vaapi") == 0 ||
            strcmp(codec, "hevc_vaapi_10bit") == 0 ||
            strcmp(codec, "av1_vaapi_10bit") == 0);
}

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

void print_usage(const CliPlatformHandle* h) {
    printf("Usage: ffmpeg_converter [options] file1 file2 ...\n\n");
    printf("Options:\n");
    printf("  -h, --help                Show this help message\n");
    printf("      --codecs-list         List all available codecs and presets\n");
    printf("      --version             Show version information\n");
    printf("\n");

    printf("  -c, --codec <group>       Codec group (software, mux, or hardware group)\n");
    printf("      --encoder <name>      Encoder inside the selected group\n");
    printf("      --preset <name>       Preset for the selected encoder\n");
    /* Legacy flat codec IDs and -p are intentionally not advertised. */
    /* Keep this group list descriptive; availability is resolved from the catalog. */
    printf("  Available groups are filtered from presets.json after hardware detection.\n");
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
    if (platform_codec_is_available(h, "prores_ks_vulkan") ||
        platform_codec_is_available(h, "h264_vulkan") ||
        platform_codec_is_available(h, "hevc_vulkan") ||
        platform_codec_is_available(h, "av1_vulkan"))
        printf("      --vk_device <N>    Vulkan adapter index for prores_ks_vulkan,"
               " h264_vulkan, hevc_vulkan, av1_vulkan"
               " (default: %d)\n",
               platform_get_default_vulkan_device(h, NULL));
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
    printf("  ffmpeg_converter -c software --encoder prores_ks --preset hq input.mov\n");
    printf("  ffmpeg_converter -a loudnorm2 -g rock input1.mov input2.mov\n");
    if (platform_m4v_is_supported())
        printf("  ffmpeg_converter -c m4v --m4v-lang eng input.mov\n");
    printf("\n");
}

static json_t *load_selection_catalog(void)
{
    const char *env = getenv("PRESETS_V2_PATH");
    char path[4096];
    json_error_t error;

    if (env && env[0] != '\0')
        return json_load_file(env, 0, &error);
    if (cli_get_presets_v2_path(path, sizeof(path)))
        return json_load_file(path, 0, &error);
    return NULL;
}

int cli_resolve_selection(const char* group, const char* encoder,
                          char* final_codec, size_t final_codec_sz)
{
    json_t *root, *selection, *common, *platforms, *platform_obj;
    json_t *group_obj = NULL, *items = NULL, *item = NULL, *value;
    const char *platform;
    int result = 0;

    if (!group || !encoder || !final_codec || final_codec_sz == 0)
        return 0;
    final_codec[0] = '\0';

    /* `copy` is the terminal stream-copy mode represented by the
     * mux.modes.copy catalog entry. It has no encoder or preset level. */
    if (!strcmp(group, "copy")) {
        strncpy(final_codec, "copy", final_codec_sz - 1);
        final_codec[final_codec_sz - 1] = '\0';
        return 1;
    }

    /* mux is a pipeline group: the CLI encoder value is its container, while
     * the converter codec remains `mux`. */
    if (!strcmp(group, "mux") &&
        (!strcmp(encoder, "mkv") || !strcmp(encoder, "mov") ||
         !strcmp(encoder, "m4v"))) {
        strncpy(final_codec, "mux", final_codec_sz - 1);
        final_codec[final_codec_sz - 1] = '\0';
        return 1;
    }
    platform = get_platform_name();
    root = load_selection_catalog();
    if (!root)
        return 0;
    selection = json_object_get(root, "selection");
    common = selection ? json_object_get(selection, "common") : NULL;
    platforms = selection ? json_object_get(selection, "platforms") : NULL;
    platform_obj = platforms ? json_object_get(platforms, platform) : NULL;
    if (common)
        group_obj = json_object_get(common, group);
    if (!group_obj && platform_obj) {
        json_t *hw = json_object_get(platform_obj, "hwaccel");
        json_t *groups = hw ? json_object_get(hw, "groups") : NULL;
        group_obj = groups ? json_object_get(groups, group) : NULL;
    }
    if (!group_obj || !json_is_true(json_object_get(group_obj, "enabled")))
        goto done;
    if (!strcmp(group, "mux"))
        items = json_object_get(group_obj, "modes");
    else
        items = json_object_get(group_obj, "encoders");
    item = items ? json_object_get(items, encoder) : NULL;
    if (!item || !json_is_true(json_object_get(item, "enabled")))
        goto done;
    value = json_object_get(item, "final_codec");
    if (!value)
        value = json_object_get(item, "execution_codec");
    if (!json_is_string(value))
        goto done;
    {
        const char *resolved = json_string_value(value);
        if (strstr(encoder, "_10bit") != NULL) {
            snprintf(final_codec, final_codec_sz, "%s_10bit", resolved);
        } else {
            strncpy(final_codec, resolved, final_codec_sz - 1);
            final_codec[final_codec_sz - 1] = '\0';
        }
    }
    result = 1;
done:
    json_decref(root);
    return result;
}

void print_version(void) {
    printf("ffmpeg_converter %s\n", FFMPEG_CONVERTER_VERSION);
}

typedef struct {
    char codec[64];
    char encoder[64];
} CliSummarySelection;

static void summary_copy(char* dst, size_t dst_sz, const char* src) {
    if (!dst || dst_sz == 0)
        return;
    if (!src)
        src = "";
    strncpy(dst, src, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
}

static int summary_item_matches_codec(json_t* item,
                                      const char* item_name,
                                      const char* final_codec) {
    json_t* value;
    const char* resolved;
    char synthetic[96];

    if (!item || !item_name || !final_codec)
        return 0;
    value = json_object_get(item, "final_codec");
    if (!value)
        value = json_object_get(item, "execution_codec");
    if (!json_is_string(value))
        return 0;
    resolved = json_string_value(value);
    if (!strcmp(final_codec, resolved))
        return 1;
    if (strstr(item_name, "_10bit") != NULL) {
        snprintf(synthetic, sizeof(synthetic), "%s_10bit", resolved);
        return !strcmp(final_codec, synthetic);
    }
    return 0;
}

static int summary_find_in_groups(json_t* groups,
                                  const char* final_codec,
                                  CliSummarySelection* out) {
    const char* group_name;
    json_t* group;

    if (!groups || !out)
        return 0;
    json_object_foreach(groups, group_name, group) {
        json_t* items;
        const char* item_name;
        json_t* item;

        if (!json_is_true(json_object_get(group, "enabled")))
            continue;
        items = json_object_get(group, "encoders");
        if (!items)
            items = json_object_get(group, "modes");
        if (!items)
            continue;
        json_object_foreach(items, item_name, item) {
            if (!json_is_true(json_object_get(item, "enabled")))
                continue;
            if (summary_item_matches_codec(item, item_name, final_codec)) {
                summary_copy(out->codec, sizeof(out->codec), group_name);
                summary_copy(out->encoder, sizeof(out->encoder), item_name);
                return 1;
            }
        }
    }
    return 0;
}

static void summary_resolve_selection(const ConvertOptions* opts,
                                      CliSummarySelection* out) {
    json_t *root, *selection, *common, *platforms, *platform_obj;
    json_t *hwaccel, *groups;

    summary_copy(out->codec, sizeof(out->codec), opts->codec);
    summary_copy(out->encoder, sizeof(out->encoder), opts->codec);

    if (!strcmp(opts->codec, "mux")) {
        summary_copy(out->codec, sizeof(out->codec), "mux");
        summary_copy(out->encoder, sizeof(out->encoder), "mux");
        return;
    }

    root = load_selection_catalog();
    if (!root)
        return;

    selection = json_object_get(root, "selection");
    common = selection ? json_object_get(selection, "common") : NULL;
    platforms = selection ? json_object_get(selection, "platforms") : NULL;
    platform_obj = platforms ? json_object_get(platforms, get_platform_name()) : NULL;

    if (summary_find_in_groups(common, opts->codec, out)) {
        json_decref(root);
        return;
    }

    hwaccel = platform_obj ? json_object_get(platform_obj, "hwaccel") : NULL;
    groups = hwaccel ? json_object_get(hwaccel, "groups") : NULL;
    summary_find_in_groups(groups, opts->codec, out);
    json_decref(root);
}

void print_summary(const ConvertOptions* opts,
                   const CliM4VOptions* m4v_opts,
                   const char** files, int file_count)
{
    int i;
    CliSummarySelection selection;
    const char* preset = opts->preset[0] != '\0' ? opts->preset : "default";

    summary_resolve_selection(opts, &selection);
    printf("\033[1;1H\033[2J");
    printf("\n=== Summary ===\n");
    printf("Codec:        %s\n", selection.codec);
    printf("Encoder:      %s\n", selection.encoder);
    printf("Preset:       %s\n", preset);

    if (!strcmp(opts->codec, "m4v")) {
        printf("Deblock:      (m4v)\n");
    } else if (!strcmp(opts->codec, "mux")) {
        printf("Deblock:      (mux)\n");
    } else if (!strcmp(opts->codec, "prores") ||
               !strcmp(opts->codec, "prores_ks")) {
        const char* deblock_str = "none";
        switch (opts->deblock) {
            case 1: deblock_str = "none";   break;
            case 2: deblock_str = "weak";   break;
            case 3: deblock_str = "strong"; break;
        }
        printf("Deblock:      %s\n", deblock_str);
    } else {
        printf("Deblock:      (n/a)\n");
        if (codec_uses_vaapi_device(opts->codec)) {
            printf("HW device:    %s\n",
                   opts->hw_device[0] != '\0' ? opts->hw_device : "(auto)");
        }
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
 *  Codec/Preset listing and validation (Task 4)
 * --------------------------------------------------------------- */

static const char* get_platform_name(void) {
#if defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

static void print_catalog_group(const char *group_name, json_t *group_obj,
                                const CliPlatformHandle *h)
{
    json_t *items;
    const char *item_name;
    json_t *item;

    if (!json_is_true(json_object_get(group_obj, "enabled")))
        return;
    items = json_object_get(group_obj, "encoders");
    if (!items)
        items = json_object_get(group_obj, "modes");
    if (!items)
        return;
    printf("%s:\n", group_name);
    json_object_foreach(items, item_name, item) {
        json_t *enabled = json_object_get(item, "enabled");
        json_t *final = json_object_get(item, "final_codec");
        if (!final)
            final = json_object_get(item, "execution_codec");
        if (!json_is_true(enabled))
            continue;
        if (json_is_string(final) &&
            platform_codec_is_available(h, json_string_value(final)))
            printf("  %s (%s)\n", item_name, json_string_value(final));
    }
}

void cli_print_codecs_list(const CliPlatformHandle* h) {
    json_t *root = load_selection_catalog();
    json_t *selection;
    json_t *common;
    json_t *platforms;
    json_t *platform_obj;
    json_t *hwaccel;
    json_t *groups;
    const char *name;
    json_t *group;

    if (!root) {
        fprintf(stderr, "Error: presets.json catalog could not be loaded\n");
        return;
    }
    selection = json_object_get(root, "selection");
    common = selection ? json_object_get(selection, "common") : NULL;
    platforms = selection ? json_object_get(selection, "platforms") : NULL;
    platform_obj = platforms ? json_object_get(platforms, get_platform_name()) : NULL;
    printf("\nAvailable codec groups and encoders for %s:\n", get_platform_name());
    printf("==============================================\n");
    if (common)
        json_object_foreach(common, name, group)
            print_catalog_group(name, group, h);
    hwaccel = platform_obj ? json_object_get(platform_obj, "hwaccel") : NULL;
    groups = hwaccel ? json_object_get(hwaccel, "groups") : NULL;
    if (groups)
        json_object_foreach(groups, name, group)
            print_catalog_group(name, group, h);
    json_decref(root);
}

int cli_validate_codec_preset(const char* codec, const char* preset) {
    PresetDb *db;
    const char *platform;
    const PresetInfo *info;
    int result = 0;
    
    if (!codec || !preset) {
        fprintf(stderr, "Error: codec and preset must not be NULL\n");
        return 0;
    }
    
    db = preset_db_load(NULL);
    if (!db) {
        fprintf(stderr, "Error: Could not load preset database\n");
        return 0;
    }
    
    platform = get_platform_name();
    info = preset_db_get(db, platform, codec, preset);
    if (info) {
        result = 1;
    } else {
        fprintf(stderr, "Error: preset '%s' is not available for codec '%s'\n",
                preset, codec);
    }
    
    preset_db_free(db);
    return result;
}

/* ---------------------------------------------------------------
 *  Argument parsing
 * --------------------------------------------------------------- */

int parse_args(int argc, char** argv, const CliPlatformHandle* h,
               ConvertOptions* opts, CliM4VOptions* m4v_opts,
               const char** files, int* file_count)
{
    int i;
    int audio_norm_explicit = 0;
    int deblock_explicit = 0;
    int preset_explicit = 0;
    char selection_group[64] = "software";
    char selection_encoder[64] = "prores_ks";

    /* Zero the struct so every field (hw_device, vulkan_device, gain,
     * measured_*, ...) has a deterministic value before defaults are set. */
    memset(opts, 0, sizeof(*opts));

    strcpy(opts->codec, "prores_ks");
    strcpy(opts->preset, "standard");  /* standard profile */
    opts->deblock   = 1;  /* none */
    strcpy(opts->audio_norm, "none");
    strcpy(opts->audio_output_mode, "pcm");
    opts->genre     = 1;  /* edm */
    opts->overwrite = 0;
    opts->output_dir[0] = '\0';
    opts->output_dir_status = 0;
    opts->video_track_path[0] = '\0';
        /* -1 = "unresolved": platform_apply_hw_device() fills in the
         * codec-correct probe recommendation once the final --codec is known.
         * A user-supplied --vk_device (parsed below) overrides this. */
        opts->vulkan_device = -1;

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

        if (!strcmp(argv[i], "--codecs-list")) {
            cli_print_codecs_list(h);
            return 0;
        }

        if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
            print_version();
            return 0;
        }

        if (!strcmp(argv[i], "--codec") || !strcmp(argv[i], "-c")) {
            if (i + 1 >= argc) return 0;
            i++;
            strncpy(selection_group, argv[i], sizeof(selection_group) - 1);
            selection_group[sizeof(selection_group) - 1] = '\0';
            continue;
        }

        if (!strcmp(argv[i], "--encoder")) {
            if (i + 1 >= argc) return 0;
            i++;
            strncpy(selection_encoder, argv[i], sizeof(selection_encoder) - 1);
            selection_encoder[sizeof(selection_encoder) - 1] = '\0';
            continue;
        }

        if (!strcmp(argv[i], "--preset")) {
            if (i + 1 >= argc) return 0;
            i++;
            strncpy(opts->preset, argv[i], sizeof(opts->preset) - 1);
            opts->preset[sizeof(opts->preset) - 1] = '\0';
            preset_explicit = 1;
            continue;
        }

        if (!strcmp(argv[i], "--deblock") || !strcmp(argv[i], "-d")) {
            if (i + 1 >= argc) return 0;
            i++;
            if      (!strcmp(argv[i], "none"))   opts->deblock = 1;
            else if (!strcmp(argv[i], "weak"))   opts->deblock = 2;
            else if (!strcmp(argv[i], "strong")) opts->deblock = 3;
            else return 0;
            deblock_explicit = 1;
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
            audio_norm_explicit = 1;
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
            if (platform_ensure_output_dir(opts->output_dir)) {
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

    if (!cli_resolve_selection(selection_group, selection_encoder,
                               opts->codec, sizeof(opts->codec))) {
        fprintf(stderr, "Error: unavailable codec group/encoder: %s/%s\n",
                selection_group, selection_encoder);
        return 0;
    }
    if (!preset_explicit) {
        if (!strcmp(opts->codec, "copy"))
            strcpy(opts->preset, "default");
        else if (!strcmp(opts->codec, "mux") &&
                 (!strcmp(selection_encoder, "mkv") ||
                  !strcmp(selection_encoder, "mov") ||
                  !strcmp(selection_encoder, "m4v")))
            strncpy(opts->preset, selection_encoder, sizeof(opts->preset) - 1);
        else if (strstr(opts->codec, "prores") != NULL)
            strcpy(opts->preset, "standard");
        else
            strcpy(opts->preset, "default");
    }

    {
        int is_hw = strstr(opts->codec, "_vaapi") != NULL ||
                    strstr(opts->codec, "_nvenc") != NULL ||
                    strstr(opts->codec, "_amf") != NULL ||
                    strstr(opts->codec, "_qsv") != NULL ||
                    strstr(opts->codec, "_vulkan") != NULL;
        if (is_hw && deblock_explicit) {
            fprintf(stderr, "Error: --deblock is not available for hardware encoder '%s'\n",
                    opts->codec);
            return 0;
        }
        if (is_hw && !audio_norm_explicit)
            strcpy(opts->audio_norm, "none");

        if (preset_explicit) {
            int valid_preset = 0;
            if (!strcmp(opts->codec, "prores_ks_vulkan"))
                valid_preset = !strcmp(opts->preset, "lt") ||
                               !strcmp(opts->preset, "standard") ||
                               !strcmp(opts->preset, "hq") ||
                               !strcmp(opts->preset, "4444");
            else if (is_hw)
                valid_preset = !strcmp(opts->preset, "default") ||
                               !strcmp(opts->preset, "speed") ||
                               !strcmp(opts->preset, "balance") ||
                               !strcmp(opts->preset, "quality");
            else
                valid_preset = cli_validate_codec_preset(opts->codec, opts->preset);
            if (!valid_preset) {
                fprintf(stderr, "Error: preset '%s' is not available for encoder '%s'\n",
                        opts->preset, opts->codec);
                return 0;
            }
        }
    }
    if (!platform_codec_is_available(h, opts->codec)) {
        fprintf(stderr, "Error: encoder is not available after hardware detection: %s\n",
                opts->codec);
        return 0;
    }
    return 1;
}
