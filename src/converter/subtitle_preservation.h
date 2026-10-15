#ifndef FFMPEG_CONVERTER_SUBTITLE_PRESERVATION_H
#define FFMPEG_CONVERTER_SUBTITLE_PRESERVATION_H

#include "converter.h"
#include "mux_policy.h"

/* Build explicit subtitle maps/encoders; fail closed if probing fails. */
int subtitle_preservation_build(const char *input, MuxContainer container,
                                char *args, size_t args_size,
                                const ConverterCallbacks *callbacks);

#endif
