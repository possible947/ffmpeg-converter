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

#ifdef __cplusplus
}
#endif

#endif
