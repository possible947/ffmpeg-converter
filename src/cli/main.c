/**
 * main.c — Unified CLI entry point for ffmpeg_converter.
 *
 * Platform-specific behaviour is handled through the cli_platform.h
 * abstraction.  All UI logic lives in cli_common.c.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "converter.h"
#include "add_track.h"
#include "hq_adapter.h"
#include "hq_catalog.h"
#include "cli_common.h"
#include "cli_platform.h"
#include "m4v.h"

#include <sys/stat.h>
#include <unistd.h>

/* Maximum number of files accepted on the command line */
#define CLI_MAX_ARG_FILES CLI_BUFFER_SIZE

int main(int argc, char** argv) {
    CliPlatformHandle* h    = NULL;
    Converter*         c    = NULL;
    const char**       files = NULL;
    const char*        arg_files[CLI_MAX_ARG_FILES];
    int                file_count = 0;
    int                result     = 0;

    printf("Hardware components detection in progress...\n");
    fflush(stdout);
    h = cli_platform_init();
    if (!h) {
        fprintf(stderr, "Failed to initialize platform support.\n");
        return 1;
    }
    printf("Hardware components detection completed.\n");

    /* Quick-exit help */
    if (argc == 2 &&
        (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        print_usage(h);
        cli_platform_cleanup(h);
        return 0;
    }

    /* Quick-exit version */
    if (argc == 2 &&
        (!strcmp(argv[1], "--version"))) {
        print_version();
        cli_platform_cleanup(h);
        return 0;
    }

    /* Quick-exit codecs list */
    if (argc == 2 &&
        (!strcmp(argv[1], "--codecs-list"))) {
        cli_print_codecs_list(h);
        cli_platform_cleanup(h);
        return 0;
    }

    c = converter_create();
    if (!c) {
        fprintf(stderr, "Failed to create converter.\n");
        cli_platform_cleanup(h);
        return 1;
    }

    {
        ConverterCallbacks cb = {
            .on_file_begin        = cli_on_file_begin,
            .on_file_end          = cli_on_file_end,
            .on_stage             = cli_on_stage,
            .on_progress_encode   = cli_on_progress_encode,
            .on_progress_analysis = cli_on_progress_analysis,
            .on_message           = cli_on_message,
            .on_error             = cli_on_error,
            .on_complete          = cli_on_complete
        };

        ConvertOptions  opts;
        CliM4VOptions   m4v_cli_opts;

        converter_set_callbacks(c, &cb);

        /* Command-line argument mode only */
        if (!parse_args(argc, argv, h, &opts, &m4v_cli_opts, arg_files, &file_count)) {
            printf("Invalid options. Use -h for help.\n");
            result = 1;
            goto cleanup;
        }
        files = arg_files;
        /* Apply platform hardware defaults (e.g. VAAPI render node) */
        platform_apply_hw_device(&opts, h);

        if (file_count == 0) {
            print_usage(h);
            result = 1;
            goto cleanup;
        }

        print_summary(&opts, &m4v_cli_opts, files, file_count);

        {
            int valid_files = verify_all_files(files, file_count);
            if (valid_files == 0) {
                result = 1;
                goto cleanup;
            }
            if (valid_files < file_count) {
                printf("Will process %d valid file(s)\n", valid_files);
                file_count = valid_files;
            }
        }

        /* Validate mux-specific inputs */
        if (!strcmp(opts.codec, "mux")) {
            if (!platform_mux_is_supported()) {
                fprintf(stderr, "Mux mode is not supported on this platform.\n");
                result = 1;
                goto cleanup;
            }
            if (file_count != 1) {
                fprintf(stderr, "Mux mode requires exactly one source file.\n");
                result = 1;
                goto cleanup;
            }
            if (!platform_file_is_regular_readable(opts.video_track_path)) {
                fprintf(stderr,
                        "Mux mode requires a readable --video-track file.\n");
                result = 1;
                goto cleanup;
            }
        }

        /* Run conversion */
        {
            ConvertOptions work_opts = opts;
            ConverterError err = ERR_OK;

            /* Dry run: print the plan and stop before touching ffmpeg. */
            if (opts.dry_run) {
                int fi;
                printf("\n--- Dry run: planned operations ---\n");
                for (fi = 0; fi < file_count; fi++) {
                    char out_name[1024];
                    if (!strcmp(opts.codec, "m4v")) {
                        M4VOptions m4v_plan_opts;
                        m4v_default_options(&m4v_plan_opts);
                        m4v_make_output_name(files[fi], opts.output_dir,
                                             out_name, sizeof(out_name));
                        printf("  [m4v] %s -> %s\n", files[fi], out_name);
                    } else {
                        converter_make_output_name(files[fi], &opts,
                                                   out_name, sizeof(out_name));
                        printf("  [%s] %s -> %s\n", opts.codec, files[fi], out_name);
                    }
                }
                printf("--- Dry run: no files were processed ---\n");
                result = 0;
                goto cleanup;
            }

            if (!strcmp(opts.codec, "m4v")) {
                /* Apple M4V path — uses the m4v module directly */
                M4VOptions m4v_opts;
                int        fi;

                if (!platform_m4v_is_supported()) {
                    fprintf(stderr,
                            "Apple M4V mode is not supported on this platform "
                            "(MP4Box not found).\n");
                    result = 1;
                    goto cleanup;
                }

                m4v_opts.video_track_index = m4v_cli_opts.video_track_index;
                m4v_opts.audio_track_index = m4v_cli_opts.audio_track_index;
                m4v_opts.ac3_bitrate_kbps  = m4v_cli_opts.ac3_bitrate_kbps;
                strncpy(m4v_opts.audio_lang, m4v_cli_opts.audio_lang,
                        sizeof(m4v_opts.audio_lang) - 1);
                m4v_opts.audio_lang[sizeof(m4v_opts.audio_lang) - 1] = '\0';
                m4v_opts.add_chapters = m4v_cli_opts.add_chapters;

                for (fi = 0; fi < file_count; fi++) {
                    char   out_file[2048];
                    char   error_text[512];
                    ConverterError ferr;

                    cli_on_file_begin(files[fi], fi + 1, file_count);

                    ferr = m4v_make_output_name(files[fi],
                                                opts.output_dir[0] != '\0'
                                                    ? opts.output_dir : NULL,
                                                out_file, sizeof(out_file));
                    if (ferr != ERR_OK) {
                        fprintf(stderr,
                                "Failed to build output name for: %s\n",
                                files[fi]);
                        cli_on_file_end(files[fi], ferr);
                        err = ferr;
                        continue;
                    }

                    ferr = m4v_create_from_input(files[fi], out_file,
                                                 &m4v_opts,
                                                 opts.overwrite,
                                                 NULL,
                                                 &cb,
                                                 error_text,
                                                 sizeof(error_text));
                    if (ferr != ERR_OK && ferr != ERR_SKIP_FILE) {
                        fprintf(stderr, "M4V error on %s: %s\n",
                                files[fi],
                                error_text[0] != '\0'
                                    ? error_text
                                    : converter_error_string(ferr));
                        if (err == ERR_OK) err = ferr;
                    }

                    cli_on_file_end(files[fi], ferr);
                }

                if (err == ERR_OK)
                    cli_on_complete();

            } else if (!strcmp(opts.codec, "hq_converter")) {
                /* HQ Converter path */
                HqAdapter *hq_adapter = NULL;
                const char *hq_encoder;
                const char *hq_preset;
                char task_dir[1024];
                int fi;

                if (!platform_hq_is_available()) {
                    fprintf(stderr,
                            "HQ Converter runtime not available.\n");
                    result = 1;
                    goto cleanup;
                }

                /* Load HQ catalog */
                if (hq_catalog_load(NULL) != 0) {
                    fprintf(stderr,
                            "Failed to load HQ preset catalog.\n");
                    result = 1;
                    goto cleanup;
                }

                /* HQ encoder is carried in hw_device field */
                hq_encoder = opts.hw_device[0] != '\0'
                                 ? opts.hw_device
                                 : NULL;
                hq_preset = opts.preset[0] != '\0'
                                ? opts.preset
                                : NULL;

                /* Validate codec/preset against HQ catalog */
                if (!hq_encoder || !hq_catalog_codec_exists(hq_encoder)) {
                    fprintf(stderr,
                            "HQ encoder '%s' not found in catalog.\n",
                            hq_encoder ? hq_encoder : "(none)");
                    result = 1;
                    goto cleanup;
                }
                if (!hq_preset) {
                    /* Use default preset */
                    hq_preset = hq_catalog_default_preset(hq_encoder);
                    if (!hq_preset) {
                        fprintf(stderr,
                                "No default preset for HQ encoder '%s'.\n",
                                hq_encoder);
                        result = 1;
                        goto cleanup;
                    }
                } else if (!hq_catalog_preset_exists(hq_encoder, hq_preset)) {
                    fprintf(stderr,
                            "HQ preset '%s' not found for encoder '%s'.\n",
                            hq_preset, hq_encoder);
                    result = 1;
                    goto cleanup;
                }

                /* Create HQ adapter */
                hq_adapter = hq_adapter_create();
                if (!hq_adapter) {
                    fprintf(stderr,
                            "Failed to create HQ adapter.\n");
                    result = 1;
                    goto cleanup;
                }

                if (file_count != 1) {
                    fprintf(stderr,
                            "HQ mode requires exactly one source file.\n");
                    result = 1;
                    goto cleanup;
                }

                for (fi = 0; fi < file_count; fi++) {
                    ConverterError ferr = ERR_OK;
                    char video_track[1024];
                    ConvertOptions hq_opts;
                    AddTrackRequest req;
                    AddTrackResult track_result;

                    cli_on_file_begin(files[fi], fi + 1, file_count);

                    /* Resolve effective output directory */
                    char effective_output_dir[1024];
                    if (opts.output_dir[0] != '\0') {
                        strncpy(effective_output_dir, opts.output_dir,
                                sizeof(effective_output_dir) - 1);
                        effective_output_dir[sizeof(effective_output_dir) - 1] = '\0';
                    } else {
                        const char *home = getenv("HOME");
                        if (home) {
                            snprintf(effective_output_dir,
                                     sizeof(effective_output_dir),
                                     "%s/ffmpeg_converter", home);
                        } else {
                            strncpy(effective_output_dir, "ffmpeg_converter",
                                    sizeof(effective_output_dir) - 1);
                        }
                    }

                    /* Ensure output directory exists */
                    if (!platform_ensure_output_dir(effective_output_dir)) {
                        fprintf(stderr,
                                "Output directory not writable: %s\n",
                                effective_output_dir);
                        ferr = ERR_OUTPUT_EXISTS;
                        cli_on_file_end(files[fi], ferr);
                        if (err == ERR_OK) err = ferr;
                        continue;
                    }

                    /* Create task_N directory */
                    snprintf(task_dir, sizeof(task_dir),
                             "%s/task_%d", effective_output_dir, fi + 1);
                    if (mkdir(task_dir, 0755) != 0 && errno != EEXIST) {
                        fprintf(stderr,
                                "Failed to create task directory: %s\n",
                                task_dir);
                        ferr = ERR_OUTPUT_EXISTS;
                        cli_on_file_end(files[fi], ferr);
                        if (err == ERR_OK) err = ferr;
                        goto cleanup; /* Cannot continue without task dir */
                    }

                    /* Run HQ job */
                    HqResult hq_rc = hq_run_job(hq_adapter,
                                                  files[fi],
                                                  task_dir,
                                                  hq_encoder,
                                                  hq_preset,
                                                  c);

                    if (hq_rc == HQ_RESULT_OK) {
                        /* Read video track path from eov2 session */
                        if (hq_read_video_track_path(task_dir,
                                                     hq_encoder,
                                                     hq_preset,
                                                     video_track,
                                                     sizeof(video_track)) != 0) {
                            fprintf(stderr,
                                    "Failed to read HQ session result.\n");
                            ferr = ERR_FFMPEG_FAILED;
                        } else if (!platform_file_is_regular_readable(
                                     video_track)) {
                            fprintf(stderr,
                                    "HQ video track not found: %s\n",
                                    video_track);
                            ferr = ERR_INPUT_NOT_FOUND;
                        } else {
                            /* Check stop flag before Add Track */
                            if (converter_is_stopped(c)) {
                                ferr = ERR_SKIP_FILE;
                            } else {
                                /* Handoff to add_track_execute() */
                                hq_opts = opts;
                                strncpy(hq_opts.video_track_path,
                                        video_track,
                                        sizeof(hq_opts.video_track_path) - 1);
                                hq_opts.video_track_path[
                                    sizeof(hq_opts.video_track_path) - 1] = '\0';

                                req.source_file = files[fi];
                                req.convert_options = &hq_opts;
                                ferr = add_track_execute(c, &req,
                                                         &track_result);
                            }
                        }
                    } else if (hq_rc == HQ_RESULT_CANCELLED) {
                        ferr = ERR_SKIP_FILE;
                    } else {
                        /* HQ failure — get error text */
                        char hq_err[512];
                        if (hq_adapter_get_last_error(hq_adapter,
                                                      hq_err,
                                                      sizeof(hq_err)) == 0 &&
                            hq_err[0] != '\0') {
                            fprintf(stderr,
                                    "HQ failed: %s\n", hq_err);
                        } else {
                            fprintf(stderr,
                                    "HQ job failed (code %d).\n",
                                    (int)hq_rc);
                        }
                        ferr = ERR_FFMPEG_FAILED;
                    }

                    cli_on_file_end(files[fi], ferr);
                    if (err == ERR_OK) err = ferr;

                    /* Remove task_N directory — always cleanup */
                    hq_remove_directory_tree(task_dir);
                }

                hq_adapter_destroy(hq_adapter);
                hq_adapter = NULL;

            } else if (!strcmp(opts.codec, "mux")) {
                AddTrackRequest req = {
                    .source_file      = files[0],
                    .convert_options  = &opts,
                };
                AddTrackResult result;
                err = add_track_execute(c, &req, &result);

            } else {
                err = converter_set_options(c, &work_opts);
                if (err != ERR_OK) {
                    fprintf(stderr, "Error: invalid options for codec '%s': %s\n",
                            work_opts.codec, converter_error_string(err));
                } else {
                    err = converter_process_files(c, files, file_count);
                }
            }

            result = (err == ERR_OK) ? 0 : 1;
        }

cleanup:
    }

    converter_destroy(c);
    cli_platform_cleanup(h);

    return result;
}
