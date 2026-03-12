#ifndef CONVERTER_PAS_H
#define CONVERTER_PAS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ERR_OK = 0,
    ERR_INPUT_NOT_FOUND,
    ERR_INPUT_NOT_REGULAR,
    ERR_INPUT_NOT_READABLE,
    ERR_OUTPUT_EXISTS,
    ERR_SKIP_FILE,
    ERR_PEAK_ANALYSIS_FAILED,
    ERR_LOUDNORM_ANALYSIS_FAILED,
    ERR_FFMPEG_FAILED,
    ERR_FFPROBE_FAILED,
    ERR_POPEN_FAILED,
    ERR_PCLOSE_FAILED,
    ERR_INVALID_OPTIONS,
    ERR_UNKNOWN
} ConverterError;

typedef struct {
    char codec[32];
    int  profile;
    int  deblock;

    char audio_norm[32];
    int  genre;

    double gain;
    double I_target;
    double TP_target;
    double LRA_target;
    double measured_I;
    double measured_TP;
    double measured_LRA;
    double measured_thresh;
    double measured_offset;

    int  overwrite;
    char output_dir[1024];
    int  output_dir_status;
    int  use_aac_for_h265;
} ConvertOptions;

typedef struct {
    void (*on_file_begin)(const char* filename, int index, int total);
    void (*on_file_end)(const char* filename, ConverterError status);
    void (*on_stage)(const char* stage_name);
    void (*on_progress_encode)(float percent, float fps, float eta_seconds);
    void (*on_progress_analysis)(float percent, float eta_seconds);
    void (*on_message)(const char* text);
    void (*on_error)(const char* text, ConverterError code);
    void (*on_complete)(void);
} ConverterCallbacks;

typedef void* Converter;

Converter converter_create(void);
void converter_destroy(Converter c);
void converter_set_callbacks(Converter c, const ConverterCallbacks* cb);
ConverterError converter_set_options(Converter c, const ConvertOptions* opts);
ConverterError converter_process_files(Converter c, const char** files, int file_count);
void converter_stop(Converter c);
const char* converter_error_string(ConverterError err);

#ifdef __cplusplus
}
#endif

#endif
