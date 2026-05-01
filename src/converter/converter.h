#ifndef CONVERTER_H
#define CONVERTER_H

#include <stddef.h>

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
    ERR_UNKNOWN,

    // PLATFORM ERRORS (added for cross-platform support)
    ERR_PLATFORM_INIT_FAILED,
    ERR_AUDIO_FILTER_VALIDATION_FAILED,
    ERR_GPU_NOT_SUPPORTED,
    ERR_PATH_TOO_LONG,
    ERR_HOME_DIR_NOT_FOUND
} ConverterError;

// ------------------------------------------------------------
//  Options
// ------------------------------------------------------------
typedef struct {
    // VIDEO
    char codec[32];     // "copy", "prores", "prores_ks",
                        // "prores_videotoolbox" (macOS),
                        // "hevc_videotoolbox"   (macOS)
    int  profile;       // 0=none, 1=lt, 2=standard, 3=hq, 4=4444
    int  deblock;       // 1=none, 2=weak, 3=strong

    // AUDIO NORMALIZATION
    char audio_norm[32]; // "none", "peak_norm", "peak_norm_2pass",
                         // "loudness_norm", "loudness_norm_2pass"
    char audio_output_mode[32]; // "pcm", "fdk_aac_q5", "fdk_aac_q5_ac3_640"

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
    char output_dir[1024]; // optional output directory ("" = default $HOME/ffmpeg_converter)
    int output_dir_status;
    char video_track_path[1024];
    char hw_device[1024];
    int video_quality;
    int use_aac_for_h265;
    int hevc_vt_bitrate_kbps;  /* calculated at runtime for hevc_videotoolbox */
    int vulkan_device;         /* Vulkan adapter index for prores_ks_vulkan (default 1) */

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

void converter_make_output_name(
    const char* input,
    const ConvertOptions* opts,
    char* out,
    size_t out_sz
);

void converter_stop(Converter* c);

const char* converter_error_string(ConverterError err);

#ifdef __cplusplus
}
#endif

#endif // CONVERTER_H
