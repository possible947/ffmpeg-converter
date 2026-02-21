#ifndef CONVERTER_H
#define CONVERTER_H

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------
//  Errors
// ------------------------------------------------------------
typedef enum {
    ERR_OK = 0,

    // FILE ERRORS
    ERR_INPUT_NOT_FOUND,
    ERR_INPUT_NOT_REGULAR,
    ERR_INPUT_NOT_READABLE,

    // OUTPUT ERRORS
    ERR_OUTPUT_EXISTS,
    ERR_SKIP_FILE,

    // ANALYSIS ERRORS
    ERR_PEAK_ANALYSIS_FAILED,
    ERR_LOUDNORM_ANALYSIS_FAILED,

    // FFMPEG ERRORS
    ERR_FFMPEG_FAILED,
    ERR_FFPROBE_FAILED,

    // SYSTEM ERRORS
    ERR_POPEN_FAILED,
    ERR_PCLOSE_FAILED,

    // INTERNAL
    ERR_INVALID_OPTIONS,
    ERR_UNKNOWN
} ConverterError;

// ------------------------------------------------------------
//  Options
// ------------------------------------------------------------
typedef struct {
    // VIDEO
    char codec[32];     // "copy", "prores", "prores_ks"
    int  profile;       // 0=none, 1=lt, 2=standard, 3=hq, 4=4444
    int  deblock;       // 1=none, 2=weak, 3=strong

    // AUDIO NORMALIZATION
    char audio_norm[32]; // "none", "peak_norm", "peak_norm_2pass",
                         // "loudness_norm", "loudness_norm_2pass"

    // LOUDNORM 2-PASS GENRE
    int genre;          // 0=none, 1..5

    // INTERNAL PARAMETERS FOR 2-PASS
    double gain;
    double I_target;
    double TP_target;
    double LRA_target;
    double measured_I;
    double measured_TP;
    double measured_LRA;
    double measured_thresh;
    double measured_offset;

    // OUTPUT
    int  overwrite;      // 0=skip, 1=force
    char output_dir[1024]; // optional output directory ("" = same as input)
    int output_dir_status;

} ConvertOptions;

// ------------------------------------------------------------
//  Callbacks
// ------------------------------------------------------------
typedef struct {

    void (*on_file_begin)(
        const char* filename,
        int index,
        int total
    );

    void (*on_file_end)(
        const char* filename,
        ConverterError status
    );

    void (*on_stage)(
        const char* stage_name
    );

    void (*on_progress_encode)(
        float percent,
        float fps,
        float eta_seconds
    );

    void (*on_progress_analysis)(
        float percent,
        float eta_seconds
    );

    void (*on_message)(
        const char* text
    );

    void (*on_error)(
        const char* text,
        ConverterError code
    );

    void (*on_complete)(void);

} ConverterCallbacks;

// ------------------------------------------------------------
//  Converter object
// ------------------------------------------------------------
typedef struct Converter Converter;

// ------------------------------------------------------------
//  API
// ------------------------------------------------------------
Converter* converter_create(void);
void converter_destroy(Converter* c);

void converter_set_callbacks(
    Converter* c,
    const ConverterCallbacks* cb
);

ConverterError converter_set_options(
    Converter* c,
    const ConvertOptions* opts
);

ConverterError converter_process_files(
    Converter* c,
    const char** files,
    int file_count
);

void converter_stop(Converter* c);

const char* converter_error_string(ConverterError err);

#ifdef __cplusplus
}
#endif

#endif // CONVERTER_H