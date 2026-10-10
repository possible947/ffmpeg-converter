#include "add_track.h"
#include "mux.h"
#include "mux_platform.h"
#include "converter_platform.h"

#include <stdlib.h>
#include <string.h>

/**
 * Resolve the effective output directory for the given options.
 * Mirrors the logic in converter.c:ensure_output_dir_writable().
 */
static void add_track_resolve_output_dir(const ConvertOptions *opts,
                                         char *out_dir,
                                         size_t out_dir_sz)
{
    if (!opts || !out_dir || out_dir_sz == 0) {
        out_dir[0] = '\0';
        return;
    }

    if (opts->output_dir[0] != '\0') {
        strncpy(out_dir, opts->output_dir, out_dir_sz - 1);
        out_dir[out_dir_sz - 1] = '\0';
        return;
    }

    const char *home = platform_get_home_dir();
    char *joined = platform_join_paths(home, "ffmpeg_converter");
    if (joined) {
        strncpy(out_dir, joined, out_dir_sz - 1);
        out_dir[out_dir_sz - 1] = '\0';
        free(joined);
    } else {
        strncpy(out_dir, "ffmpeg_converter", out_dir_sz - 1);
        out_dir[out_dir_sz - 1] = '\0';
    }
}

ConverterError add_track_execute(Converter *converter,
                                  const AddTrackRequest *request,
                                  AddTrackResult *result)
{
    ConverterError err;
    ConvertOptions work_opts;
    ConvertOptions final_opts;
    MuxOptions mux_opts;
    char effective_output_dir[1024];
    char intermediate_file[1024];
    char final_file[1024];

    if (!converter || !request || !result)
        return ERR_INVALID_OPTIONS;

    if (!request->source_file || !request->source_file[0])
        return ERR_INPUT_NOT_FOUND;

    if (!request->convert_options)
        return ERR_INVALID_OPTIONS;

    if (!platform_file_is_regular(request->source_file))
        return ERR_INPUT_NOT_FOUND;

    if (!request->convert_options->video_track_path[0])
        return ERR_INVALID_OPTIONS;

    if (!platform_file_is_regular(request->convert_options->video_track_path))
        return ERR_INPUT_NOT_FOUND;

    memset(result, 0, sizeof(*result));

    work_opts = *request->convert_options;
    work_opts.codec[0] = '\0';
    strcpy(work_opts.codec, "copy");
    work_opts.preset[0] = '\0';
    strcpy(work_opts.preset, "default");
    work_opts.deblock = 0;

    err = converter_set_options(converter, &work_opts);
    if (err != ERR_OK)
        return err;

    if (converter_is_stopped(converter))
        return ERR_SKIP_FILE;

    {
        const char *files[] = { request->source_file };
        err = converter_process_files(converter, files, 1);
        if (err != ERR_OK)
            return err;
    }

    if (converter_is_stopped(converter))
        return ERR_SKIP_FILE;

    add_track_resolve_output_dir(request->convert_options,
                                 effective_output_dir,
                                 sizeof(effective_output_dir));

    work_opts.output_dir[0] = '\0';
    strncpy(work_opts.output_dir, effective_output_dir,
            sizeof(work_opts.output_dir) - 1);
    work_opts.output_dir[sizeof(work_opts.output_dir) - 1] = '\0';

    converter_make_output_name(request->source_file, &work_opts,
                               intermediate_file, sizeof(intermediate_file));

    final_opts = *request->convert_options;
    final_opts.output_dir[0] = '\0';
    strncpy(final_opts.output_dir, effective_output_dir,
            sizeof(final_opts.output_dir) - 1);
    final_opts.output_dir[sizeof(final_opts.output_dir) - 1] = '\0';

    converter_make_output_name(request->source_file, &final_opts,
                               final_file, sizeof(final_file));

    memset(&mux_opts, 0, sizeof(mux_opts));
    strncpy(mux_opts.intermediate_file, intermediate_file,
            sizeof(mux_opts.intermediate_file) - 1);
    mux_opts.intermediate_file[sizeof(mux_opts.intermediate_file) - 1] = '\0';

    strncpy(mux_opts.video_track_file,
            request->convert_options->video_track_path,
            sizeof(mux_opts.video_track_file) - 1);
    mux_opts.video_track_file[sizeof(mux_opts.video_track_file) - 1] = '\0';

    mux_opts.overwrite = request->convert_options->overwrite;

    err = mux_run_postprocess_for_preset(&mux_opts,
                                          request->convert_options,
                                          final_file,
                                          converter_get_callbacks(converter));
    if (err != ERR_OK)
        return err;

    strncpy(result->output_file, final_file, sizeof(result->output_file) - 1);
    result->output_file[sizeof(result->output_file) - 1] = '\0';

    return ERR_OK;
}
