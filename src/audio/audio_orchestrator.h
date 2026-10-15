/* audio_orchestrator.h
 *
 * Procedural audio policy and analysis module extracted from converter.c.
 * Phase 3B: behavior-preserving extraction only.
 *
 * Rules:
 *  - No object lifecycle (no create/destroy).
 *  - No second FFmpeg command builder.
 *  - Reuses ConverterCallbacks for progress reporting.
 *  - AAC capability cache is private static state (process-wide lazy cache).
 */

#ifndef AUDIO_ORCHESTRATOR_H
#define AUDIO_ORCHESTRATOR_H

#include <stddef.h>
#include "converter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------
 *  Enums
 * --------------------------------------------------------------- */

typedef enum {
    AUDIO_MODE_PCM = 0,
    AUDIO_MODE_COPY,
    AUDIO_MODE_FDK_AAC_320,
    AUDIO_MODE_FDK_AAC_320_AC3_640,
    AUDIO_MODE_INVALID
} AudioOutputMode;

typedef enum {
    AUDIO_NORM_NONE = 0,
    AUDIO_NORM_PEAK,
    AUDIO_NORM_PEAK_2PASS,
    AUDIO_NORM_LOUDNESS,
    AUDIO_NORM_LOUDNESS_2PASS,
    AUDIO_NORM_INVALID
} AudioNormalizationMode;

typedef enum {
    AUDIO_GENRE_NONE = 0,
    AUDIO_GENRE_EDM,
    AUDIO_GENRE_ROCK,
    AUDIO_GENRE_HIPHOP,
    AUDIO_GENRE_CLASSICAL,
    AUDIO_GENRE_PODCAST,
    AUDIO_GENRE_INVALID
} AudioGenre;

typedef enum {
    AUDIO_ENCODER_AAC_AT = 0,
    AUDIO_ENCODER_LIBFDK_AAC,
    AUDIO_ENCODER_AAC_NATIVE,
    AUDIO_ENCODER_NONE
} AudioEncoder;

typedef enum {
    AUDIO_TRACK_POLICY_FIRST = 0,  /* -map 0:a:0 — first audio track only */
    AUDIO_TRACK_POLICY_ALL           /* -map 0:a   — all audio tracks */
} AudioTrackPolicy;

/* ---------------------------------------------------------------
 *  Result and Configuration Structures
 * --------------------------------------------------------------- */

typedef struct {
    int has_aac_at;
    int has_libfdk_aac;
    int has_aac_native;
} AudioEncoderCaps;

typedef struct {
    double max_volume;
    double gain;
    int valid;
} PeakAnalysisResult;

typedef struct {
    double I_target;
    double TP_target;
    double LRA_target;
    double measured_I;
    double measured_TP;
    double measured_LRA;
    double measured_thresh;
    double target_offset;
    int valid;
} LoudnormAnalysisResult;

typedef struct {
    AudioOutputMode mode;
    AudioEncoder encoder;
    AudioTrackPolicy track_policy;
    int is_dual_output;
    const char *encoder_name;
    int bitrate;
    int sample_rate;
} AudioConfiguration;

/* ---------------------------------------------------------------
 *  Mode and Policy Functions
 * --------------------------------------------------------------- */

/** Parse an audio output mode string. Returns AUDIO_MODE_INVALID on failure. */
AudioOutputMode audio_output_mode_parse(const char *value);

/** Returns 1 if the mode string is valid (empty string or known mode), 0 otherwise. */
int audio_output_mode_is_valid(const char *value);

/** Parse a normalization mode string. Returns AUDIO_NORM_INVALID on failure. */
AudioNormalizationMode audio_normalization_mode_parse(const char *value);

/**
 * Return the track selection policy for the given audio output mode.
 * COPY mode selects all audio tracks; all other modes select the first.
 */
AudioTrackPolicy audio_get_track_policy(AudioOutputMode mode);

/** Map an integer genre index to an AudioGenre enum. */
AudioGenre audio_genre_parse(int value);

/**
 * Return the loudnorm target triple for a genre.
 * Returns 1 on success, 0 if the genre is unknown.
 */
int audio_genre_get_targets(
    AudioGenre genre,
    double *I_target,
    double *TP_target,
    double *LRA_target
);

/* ---------------------------------------------------------------
 *  AAC Encoder Capability Functions
 * --------------------------------------------------------------- */

/**
 * Probe FFmpeg for AAC encoder availability and populate out_caps.
 * Uses a process-wide lazy cache internally; repeated calls are cheap.
 * Returns 0 on success, -1 on error.
 */
int audio_probe_encoders(AudioEncoderCaps *out_caps);

/**
 * Returns 1 if the given encoder is available in the capability set, 0 otherwise.
 */
int audio_encoder_is_available(
    const AudioEncoderCaps *caps,
    AudioEncoder encoder
);

/**
 * Select the preferred AAC encoder for the given audio output mode.
 * Priority: aac_at -> libfdk_aac -> native aac (where permitted).
 * Returns AUDIO_ENCODER_NONE if no suitable encoder is available.
 */
AudioEncoder audio_select_preferred_encoder(
    const AudioEncoderCaps *caps,
    AudioOutputMode mode
);

/* ---------------------------------------------------------------
 *  Audio Configuration Resolution
 * --------------------------------------------------------------- */

/**
 * Resolve the full audio configuration for a given mode and encoder caps.
 * Populates out_config with encoder, encoder name, bitrate, sample rate,
 * and dual-output status.
 * Returns 0 on success, -1 on failure.
 */
int audio_resolve_configuration(
    AudioOutputMode mode,
    const AudioEncoderCaps *caps,
    AudioConfiguration *out_config
);

/* ---------------------------------------------------------------
 *  Filter Expression Generation
 * --------------------------------------------------------------- */

/**
 * Build the audio filter expression for the given normalization mode and
 * analysis results (when applicable).
 *
 * peak_result   - valid for peak_norm_2pass mode (may be NULL otherwise)
 * loudnorm_result - valid for loudness_norm_2pass mode (may be NULL otherwise)
 *
 * The expression is written into the caller-owned output buffer.
 * Returns 0 on success, -1 on failure (e.g. buffer too small).
 */
int audio_build_filter_expression(
    AudioNormalizationMode mode,
    const PeakAnalysisResult *peak_result,
    const LoudnormAnalysisResult *loudnorm_result,
    char *output,
    size_t output_size
);

/* ---------------------------------------------------------------
 *  Analysis Functions
 * --------------------------------------------------------------- */

/**
 * Run peak (volumedetect) analysis on the input file.
 * Uses the existing ConverterCallbacks for progress and error reporting.
 * stop_flag is a pointer to an int (non-volatile) that, when set to 1,
 * aborts the analysis early and returns ERR_SKIP_FILE.
 *
 * Returns ConverterError; populates out_result on success.
 */
ConverterError audio_peak_analyze(
    const char *input_path,
    const ConverterCallbacks *callbacks,
    int *stop_flag,
    PeakAnalysisResult *out_result
);

/**
 * Run loudnorm two-pass analysis on the input file.
 * Uses the existing ConverterCallbacks for progress and error reporting.
 *
 * Returns ConverterError; populates out_result on success.
 */
ConverterError audio_loudnorm_analyze(
    const char *input_path,
    AudioGenre genre,
    const ConverterCallbacks *callbacks,
    int *stop_flag,
    LoudnormAnalysisResult *out_result
);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_ORCHESTRATOR_H */
