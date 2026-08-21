#ifndef FFMPEG_CONVERTER_MUX_H
#define FFMPEG_CONVERTER_MUX_H

#include "converter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char intermediate_file[1024];
    char video_track_file[1024];
    char output_file[1024];
    int overwrite;
} MuxOptions;

ConverterError mux_run_postprocess(
    const MuxOptions* opts,
    const ConvertOptions* convert_opts,
    const ConverterCallbacks* callbacks
);

/* Preset-aware mux entry point: dispatches to the correct final container
 * (mkv/mov/m4v) based on convert_opts->preset. See mux.c for details. */
ConverterError mux_run_postprocess_for_preset(
    const MuxOptions* opts,
    const ConvertOptions* convert_opts,
    const char* final_output_file,
    const ConverterCallbacks* callbacks
);

#ifdef __cplusplus
}
#endif

#endif
