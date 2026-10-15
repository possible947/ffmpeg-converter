#ifndef FFMPEG_CONVERTER_INTERNAL_H
#define FFMPEG_CONVERTER_INTERNAL_H

#include "converter.h"

ConverterError converter_process_single_file(Converter *converter,
                                             const char *file);

#endif
