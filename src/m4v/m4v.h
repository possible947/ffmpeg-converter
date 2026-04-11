#ifndef M4V_H
#define M4V_H

#include "converter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int video_track_index;
    int audio_track_index;
    int aac_quality;
    int ac3_bitrate_kbps;
    int add_chapters;
    char audio_lang[16];
} M4VOptions;

void m4v_default_options(M4VOptions *opts);

ConverterError m4v_make_output_name(const char *input_file,
                                    const char *output_dir,
                                    char *out_file,
                                    size_t out_file_sz);

ConverterError m4v_validate_input_supported(const char *input_file,
                                            char *detail,
                                            size_t detail_sz);

ConverterError m4v_create_from_input(const char *input_file,
                                     const char *output_file,
                                     const M4VOptions *opts,
                                     int overwrite,
                                     volatile int *stop_flag,
                                     const ConverterCallbacks *callbacks,
                                     char *error_text,
                                     size_t error_text_sz);

#ifdef __cplusplus
}
#endif

#endif