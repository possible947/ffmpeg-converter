/**
 * main.c — Unified CLI entry point for ffmpeg_converter.
 *
 * Platform-specific behaviour is handled through the cli_platform.h
 * abstraction.  All UI logic lives in cli_common.c.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "converter.h"
#include "cli_common.h"
#include "cli_platform.h"
#include "m4v.h"

/* Maximum number of files accepted on the command line */
#define CLI_MAX_ARG_FILES CLI_BUFFER_SIZE

int main(int argc, char** argv) {
    CliPlatformHandle* h          = NULL;
    Converter*         c          = NULL;
    const char**       files      = NULL;
    const char*        arg_files[CLI_MAX_ARG_FILES];
    int                file_count = 0;
    int                result     = 0;
    char**             utf8_argv  = NULL;
    int                utf8_needs_free = 0;

    /* Replace argv with properly-encoded UTF-8 strings.
     * On Windows this uses GetCommandLineW so paths with non-ANSI
     * characters (Cyrillic, special symbols) survive argument passing. */
    utf8_argv = platform_utf8_argv(argc, argv, &utf8_needs_free);
    if (utf8_argv)
        argv = utf8_argv;

    h = cli_platform_init();
    if (!h) {
        fprintf(stderr, "Failed to initialize platform support.\n");
        return 1;
    }

    /* Quick-exit help */
    if (argc == 2 &&
        (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        print_usage(h);
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
        int menu_result = 0;

        converter_set_callbacks(c, &cb);

        if (argc == 1) {
            /* Interactive menu mode */
            menu_result = run_menu(h, &opts, &m4v_cli_opts, &files, &file_count);
            if (menu_result < 0) {
                printf("Menu cancelled by user.\n");
                result = 1;
                goto cleanup;
            }
            if (file_count == 0) {
                printf("No files selected.\n");
                result = 1;
                goto cleanup;
            }
        } else {
            /* Command-line argument mode */
            if (!parse_args(argc, argv, h, &opts, &m4v_cli_opts, arg_files, &file_count)) {
                printf("Invalid options. Use -h for help.\n");
                result = 1;
                goto cleanup;
            }
            files = arg_files;
            /* Apply platform hardware defaults (e.g. VAAPI render node) */
            platform_apply_hw_device(&opts, h);
        }

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
                m4v_opts.aac_quality       = m4v_cli_opts.aac_quality;
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

            } else if (!strcmp(opts.codec, "mux")) {
                strcpy(work_opts.codec, "copy");
                work_opts.profile = 0;
                work_opts.deblock = 0;

                converter_set_options(c, &work_opts);
                err = converter_process_files(c, files, file_count);

                if (err == ERR_OK)
                    err = platform_run_mux_postprocess(&opts, &cb, files[0]);

            } else {
                converter_set_options(c, &work_opts);
                err = converter_process_files(c, files, file_count);
            }

            result = (err == ERR_OK) ? 0 : 1;
        }

cleanup:
        /* Free memory allocated by run_menu */
        if (argc == 1 && files) {
            int i;
            for (i = 0; i < file_count; i++)
                free((void*)files[i]);
            free((void*)files);
        }
    }

    converter_destroy(c);
    cli_platform_cleanup(h);

    if (utf8_needs_free && utf8_argv) {
        int i;
        for (i = 0; i < argc; i++) free(utf8_argv[i]);
        free(utf8_argv);
    }

    return result;
}
