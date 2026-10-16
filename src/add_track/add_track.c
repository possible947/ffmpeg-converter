#include "add_track.h"
#include "mux.h"
#include "mux_platform.h"
#include "converter_platform.h"
#include "hq_catalog.h"
#include "mux_policy.h"
#include "converter_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

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

static int add_track_intermediate_is_usable(const char *path)
{
    const char *ffprobe = platform_get_ffprobe_bin();
    char *quoted_tool;
    char *quoted_path;
    char command[8192];
    FILE *pipe;
    json_t *document;
    json_t *streams;
    json_t *stream;
    size_t index;
    int has_video = 0;
    int has_audio = 0;
    int status;

    if (!path || !platform_file_is_regular(path) || !ffprobe || !ffprobe[0])
        return 0;
    quoted_tool = platform_escape_path_for_command(ffprobe);
    quoted_path = platform_escape_path_for_command(path);
    if (!quoted_tool || !quoted_path) {
        free(quoted_tool);
        free(quoted_path);
        return 0;
    }
    if (snprintf(command, sizeof(command),
                 "%s -v error -show_entries stream=codec_type -of json %s 2>&1",
                 quoted_tool, quoted_path) >= (int)sizeof(command)) {
        free(quoted_tool);
        free(quoted_path);
        return 0;
    }
    free(quoted_tool);
    free(quoted_path);

    pipe = platform_popen(command, "r");
    if (!pipe)
        return 0;
    document = json_loadf(pipe, 0, NULL);
    status = platform_pclose(pipe);
    if (!document) {
        return 0;
    }
    streams = json_is_object(document)
                  ? json_object_get(document, "streams") : NULL;
    if (status != 0 || !json_is_array(streams)) {
        json_decref(document);
        return 0;
    }
    json_array_foreach(streams, index, stream) {
        const char *type = json_string_value(json_object_get(stream, "codec_type"));
        if (type && strcmp(type, "video") == 0)
            has_video = 1;
        else if (type && strcmp(type, "audio") == 0)
            has_audio = 1;
    }
    json_decref(document);
    return has_video && has_audio;
}

ConverterError add_track_execute(Converter *converter,
                                  const AddTrackRequest *request,
                                  AddTrackResult *result)
{
    ConverterError err;
    ConvertOptions work_opts;
    ConvertOptions final_opts;
    MuxOptions mux_opts;
    ConvertOptions mux_policy_opts;
    MuxPolicy policy;
    MuxBuildProfile profile;
    MuxVideoCodec replacement_codec;
    char effective_output_dir[1024];
    char intermediate_file[1024];
    char final_file[1024];
    char policy_error[256];
    MuxMode mux_mode;
    int is_hq_prores;

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

    is_hq_prores =
        !strcmp(request->convert_options->codec, "hq_converter") &&
        hq_catalog_is_prores_codec(request->convert_options->hw_device);
    if (!mux_build_profile_default(&profile))
        return ERR_INVALID_OPTIONS;
    err = mux_probe_video_codec(request->convert_options->video_track_path,
                                &replacement_codec);
    if (err != ERR_OK)
        return err;

    if (!strcmp(request->convert_options->codec, "hq_converter")) {
        if (!mux_policy_resolve_hq(&profile, replacement_codec,
                                   request->convert_options->audio_output_mode,
                                   &policy, policy_error, sizeof(policy_error))) {
            const ConverterCallbacks *callbacks = converter_get_callbacks(converter);
            if (callbacks && callbacks->on_error)
                callbacks->on_error(policy_error, ERR_INVALID_OPTIONS);
            return ERR_INVALID_OPTIONS;
        }
        mux_mode = policy.mode;
    } else {
        const char *requested_mode = request->convert_options->preset;
        mux_mode = (!requested_mode || requested_mode[0] == '\0' ||
                    !strcmp(requested_mode, "default"))
                       ? MUX_MODE_MKV : mux_mode_parse(requested_mode);
        if (!mux_policy_resolve(&profile, mux_mode,
                                request->convert_options->audio_output_mode,
                                MUX_VIDEO_UNKNOWN, replacement_codec,
                                &policy, policy_error, sizeof(policy_error))) {
            const ConverterCallbacks *callbacks = converter_get_callbacks(converter);
            if (callbacks && callbacks->on_error)
                callbacks->on_error(policy_error, ERR_INVALID_OPTIONS);
            return ERR_INVALID_OPTIONS;
        }
    }
    AudioModeAvailability available = audio_mode_availability(
        &profile,
        !strcmp(request->convert_options->codec, "hq_converter")
            ? AUDIO_WORKFLOW_HQ : AUDIO_WORKFLOW_MUX,
        mux_mode, NULL, 0, replacement_codec);
    if (!available.count ||
        (request->convert_options->audio_output_mode[0] &&
         !audio_mode_availability_contains(
             &available, mux_audio_mode_parse(
                 request->convert_options->audio_output_mode)))) {
        const ConverterCallbacks *callbacks = converter_get_callbacks(converter);
        if (callbacks && callbacks->on_error)
            callbacks->on_error("audio mode is not available for this workflow",
                                ERR_INVALID_OPTIONS);
        return ERR_INVALID_OPTIONS;
    }
    if (!platform_get_mkvmerge_bin() ||
        platform_get_mkvmerge_bin()[0] == '\0' ||
        (policy.container == MUX_CONTAINER_M4V &&
         (!platform_get_mp4box_bin() || platform_get_mp4box_bin()[0] == '\0'))) {
        const ConverterCallbacks *callbacks = converter_get_callbacks(converter);
        const char *message = policy.container == MUX_CONTAINER_M4V
                                  ? "M4V mode requires mkvmerge and MP4Box"
                                  : "replacement mux mode requires mkvmerge";
        if (callbacks && callbacks->on_error)
            callbacks->on_error(message, ERR_INVALID_OPTIONS);
        return ERR_INVALID_OPTIONS;
    }

    memset(result, 0, sizeof(*result));

    work_opts = *request->convert_options;
    snprintf(work_opts.codec, sizeof(work_opts.codec), "copy");
    /* The source video is discarded by the replacement stage. Prepare its
     * audio and other source streams in a container-neutral MKV instead of
     * imposing the final MOV/M4V codec restrictions on it. */
    strncpy(work_opts.preset, mux_mode_name(MUX_MODE_MKV),
            sizeof(work_opts.preset) - 1);
    work_opts.preset[sizeof(work_opts.preset) - 1] = '\0';
    strncpy(work_opts.audio_output_mode, mux_audio_mode_name(policy.audio_mode),
            sizeof(work_opts.audio_output_mode) - 1);
    work_opts.audio_output_mode[sizeof(work_opts.audio_output_mode) - 1] = '\0';
    work_opts.deblock = 0;

    add_track_resolve_output_dir(request->convert_options,
                                 effective_output_dir,
                                 sizeof(effective_output_dir));
    strncpy(work_opts.output_dir, effective_output_dir,
            sizeof(work_opts.output_dir) - 1);
    work_opts.output_dir[sizeof(work_opts.output_dir) - 1] = '\0';
    converter_make_output_name(request->source_file, &work_opts,
                               intermediate_file, sizeof(intermediate_file));

    err = converter_set_options(converter, &work_opts);
    if (err != ERR_OK)
        return err;

    if (converter_is_stopped(converter))
        return ERR_SKIP_FILE;

    {
        err = converter_process_single_file(converter, request->source_file);
        if (err != ERR_OK) {
            if (err != ERR_SKIP_FILE || converter_is_stopped(converter))
                platform_unlink(intermediate_file);
            return err;
        }
    }

    if (converter_is_stopped(converter)) {
        platform_unlink(intermediate_file);
        return ERR_SKIP_FILE;
    }

    if (!add_track_intermediate_is_usable(intermediate_file)) {
        const ConverterCallbacks *callbacks = converter_get_callbacks(converter);
        platform_unlink(intermediate_file);
        if (callbacks && callbacks->on_error)
            callbacks->on_error("Add Track source preparation produced no usable intermediate",
                                ERR_FFPROBE_FAILED);
        return ERR_FFPROBE_FAILED;
    }

    final_opts = *request->convert_options;
    final_opts.output_dir[0] = '\0';
    strncpy(final_opts.output_dir, effective_output_dir,
            sizeof(final_opts.output_dir) - 1);
    final_opts.output_dir[sizeof(final_opts.output_dir) - 1] = '\0';

    /* HQ Converter jobs carry the real encoder id in hw_device and an
     * encoder preset name (not a container keyword) in preset — neither
     * converter_make_output_name()'s codec check nor
     * mux_run_postprocess_for_preset()'s preset check recognize these
     * HQ-specific field meanings. Detect the ProRes HQ case here and
     * substitute container-selection values understood by those two
     * functions, without touching request->convert_options itself. */
    {
        if (is_hq_prores) {
            strncpy(final_opts.codec, "prores_ks", sizeof(final_opts.codec) - 1);
            final_opts.codec[sizeof(final_opts.codec) - 1] = '\0';
        }

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

        mux_policy_opts = *request->convert_options;
        strncpy(mux_policy_opts.preset, mux_mode_name(policy.mode),
                sizeof(mux_policy_opts.preset) - 1);
        mux_policy_opts.preset[sizeof(mux_policy_opts.preset) - 1] = '\0';
        strncpy(mux_policy_opts.audio_output_mode,
                mux_audio_mode_name(policy.audio_mode),
                sizeof(mux_policy_opts.audio_output_mode) - 1);
        mux_policy_opts.audio_output_mode[
            sizeof(mux_policy_opts.audio_output_mode) - 1] = '\0';

        err = mux_run_postprocess_for_preset(&mux_opts,
                                              &mux_policy_opts,
                                              final_file,
                                              converter_get_callbacks(converter));
    }
    if (err != ERR_OK)
        return err;

    /* Remove the preparation intermediate when post-mux wrote a different
     * final path, without deleting the final output when paths are identical. */
    if (strcmp(intermediate_file, final_file) != 0)
        platform_unlink(intermediate_file);

    strncpy(result->output_file, final_file, sizeof(result->output_file) - 1);
    result->output_file[sizeof(result->output_file) - 1] = '\0';

    return ERR_OK;
}
