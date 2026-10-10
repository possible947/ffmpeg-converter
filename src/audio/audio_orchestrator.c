/* audio_orchestrator.c
 *
 * Procedural audio policy and analysis module.
 * Extracted from converter.c in Phase 3B (behavior-preserving extraction).
 *
 * No object lifecycle. No second FFmpeg command builder.
 * AAC capability cache is a private process-wide lazy cache.
 */

#include "audio_orchestrator.h"
#include "converter_platform.h"
#include "converter_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <jansson.h>
#include <time.h>

/* ================================================================
 *  Mode helpers
 * ================================================================ */

static int audio_mode_string_is(const char *mode, const char *expected) {
    return mode && expected && strcmp(mode, expected) == 0;
}

AudioOutputMode audio_output_mode_parse(const char *value) {
    if (!value) return AUDIO_MODE_INVALID;
    if (audio_mode_string_is(value, "pcm")) return AUDIO_MODE_PCM;
    if (audio_mode_string_is(value, "fdk_aac_320")) return AUDIO_MODE_FDK_AAC_320;
    if (audio_mode_string_is(value, "fdk_aac_320_ac3_640")) return AUDIO_MODE_FDK_AAC_320_AC3_640;
    return AUDIO_MODE_INVALID;
}

int audio_output_mode_is_valid(const char *value) {
    return value[0] == '\0' ||
           audio_output_mode_parse(value) != AUDIO_MODE_INVALID;
}

AudioNormalizationMode audio_normalization_mode_parse(const char *value) {
    if (!value) return AUDIO_NORM_INVALID;
    if (strcmp(value, "none") == 0) return AUDIO_NORM_NONE;
    if (strcmp(value, "peak_norm") == 0) return AUDIO_NORM_PEAK;
    if (strcmp(value, "peak_norm_2pass") == 0) return AUDIO_NORM_PEAK_2PASS;
    if (strcmp(value, "loudness_norm") == 0) return AUDIO_NORM_LOUDNESS;
    if (strcmp(value, "loudness_norm_2pass") == 0) return AUDIO_NORM_LOUDNESS_2PASS;
    return AUDIO_NORM_INVALID;
}

AudioGenre audio_genre_parse(int value) {
    switch (value) {
        case 0: return AUDIO_GENRE_NONE;
        case 1: return AUDIO_GENRE_EDM;
        case 2: return AUDIO_GENRE_ROCK;
        case 3: return AUDIO_GENRE_HIPHOP;
        case 4: return AUDIO_GENRE_CLASSICAL;
        case 5: return AUDIO_GENRE_PODCAST;
        default: return AUDIO_GENRE_INVALID;
    }
}

int audio_genre_get_targets(AudioGenre genre, double *I_target,
                            double *TP_target, double *LRA_target) {
    switch (genre) {
        case AUDIO_GENRE_NONE:
            *I_target  = -11;
            *TP_target = -1.5;
            *LRA_target = 7;
            return 1;
        case AUDIO_GENRE_EDM:
            *I_target  = -11;
            *TP_target = -1.5;
            *LRA_target = 6;
            return 1;
        case AUDIO_GENRE_ROCK:
            *I_target  = -11;
            *TP_target = -1.0;
            *LRA_target = 7;
            return 1;
        case AUDIO_GENRE_HIPHOP:
            *I_target  = -12;
            *TP_target = -1.0;
            *LRA_target = 6;
            return 1;
        case AUDIO_GENRE_CLASSICAL:
            *I_target  = -16;
            *TP_target = -2.0;
            *LRA_target = 12;
            return 1;
        case AUDIO_GENRE_PODCAST:
            *I_target  = -16;
            *TP_target = -1.5;
            *LRA_target = 7;
            return 1;
        default:
            return 0;
    }
}

/* ================================================================
 *  AAC Encoder Capability (process-wide lazy cache)
 * ================================================================ */

/* Private cache state */
static int aac_caps_initialized = 0;
static int aac_caps_has_aac_at = 0;
static int aac_caps_has_libfdk_aac = 0;
static int aac_caps_has_aac_native = 0;

static void aac_caps_run_probe(void) {
    const char *ffmpeg_bin = platform_get_ffmpeg_bin();
    char *esc_ffmpeg = platform_escape_path_for_command(ffmpeg_bin);
    if (esc_ffmpeg) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd),
                 "%s -hide_banner -v error -encoders 2>%s",
                 esc_ffmpeg, platform_get_null_device());
        free(esc_ffmpeg);

        FILE *fp = platform_popen(cmd, "r");
        if (fp) {
            char line[1024];
            while (fgets(line, sizeof(line), fp)) {
                if (!aac_caps_has_aac_at && strstr(line, " aac_at")) {
                    aac_caps_has_aac_at = 1;
                }
                if (!aac_caps_has_libfdk_aac && strstr(line, " libfdk_aac")) {
                    aac_caps_has_libfdk_aac = 1;
                }
                if (!aac_caps_has_aac_native && strstr(line, " aac ")) {
                    aac_caps_has_aac_native = 1;
                }
            }
            platform_pclose(fp);
        }
    }
    aac_caps_initialized = 1;
}

int audio_probe_encoders(AudioEncoderCaps *out_caps) {
    if (!out_caps) return -1;

    if (!aac_caps_initialized) {
        aac_caps_run_probe();
    }

    out_caps->has_aac_at = aac_caps_has_aac_at;
    out_caps->has_libfdk_aac = aac_caps_has_libfdk_aac;
    out_caps->has_aac_native = aac_caps_has_aac_native;

    return 0;
}

int audio_encoder_is_available(const AudioEncoderCaps *caps, AudioEncoder encoder) {
    if (!caps) return 0;
    switch (encoder) {
        case AUDIO_ENCODER_AAC_AT:
            return caps->has_aac_at;
        case AUDIO_ENCODER_LIBFDK_AAC:
            return caps->has_libfdk_aac;
        case AUDIO_ENCODER_AAC_NATIVE:
            return caps->has_aac_native;
        default:
            return 0;
    }
}

AudioEncoder audio_select_preferred_encoder(
    const AudioEncoderCaps *caps,
    AudioOutputMode mode)
{
    if (!caps) return AUDIO_ENCODER_NONE;

    /* For fdk modes: prefer aac_at, then libfdk_aac.
     * Native aac is NOT a fallback for fdk modes (preserves existing behavior). */
    if (mode == AUDIO_MODE_FDK_AAC_320 || mode == AUDIO_MODE_FDK_AAC_320_AC3_640) {
        if (caps->has_aac_at) return AUDIO_ENCODER_AAC_AT;
        if (caps->has_libfdk_aac) return AUDIO_ENCODER_LIBFDK_AAC;
        return AUDIO_ENCODER_NONE;
    }

    return AUDIO_ENCODER_NONE;
}

/* ================================================================
 *  Audio Configuration Resolution
 * ================================================================ */

int audio_resolve_configuration(
    AudioOutputMode mode,
    const AudioEncoderCaps *caps,
    AudioConfiguration *out_config)
{
    if (!out_config || !caps) return -1;

    out_config->mode = mode;
    out_config->encoder = AUDIO_ENCODER_NONE;
    out_config->encoder_name = "";
    out_config->is_dual_output = 0;
    out_config->bitrate = 0;
    out_config->sample_rate = 48000;

    switch (mode) {
        case AUDIO_MODE_PCM:
            out_config->encoder_name = "pcm_s16le";
            out_config->sample_rate = 48000;
            out_config->bitrate = 0;
            break;

        case AUDIO_MODE_FDK_AAC_320: {
            AudioEncoder enc = audio_select_preferred_encoder(caps, mode);
            if (enc == AUDIO_ENCODER_NONE) return -1;
            out_config->encoder = enc;
            if (enc == AUDIO_ENCODER_AAC_AT) {
                out_config->encoder_name = "aac_at";
            } else if (enc == AUDIO_ENCODER_LIBFDK_AAC) {
                out_config->encoder_name = "libfdk_aac";
            }
            out_config->bitrate = 320;
            out_config->sample_rate = 48000;
            break;
        }

        case AUDIO_MODE_FDK_AAC_320_AC3_640: {
            AudioEncoder enc = audio_select_preferred_encoder(caps, mode);
            if (enc == AUDIO_ENCODER_NONE) return -1;
            out_config->encoder = enc;
            if (enc == AUDIO_ENCODER_AAC_AT) {
                out_config->encoder_name = "aac_at";
            } else if (enc == AUDIO_ENCODER_LIBFDK_AAC) {
                out_config->encoder_name = "libfdk_aac";
            }
            out_config->is_dual_output = 1;
            out_config->bitrate = 320;
            out_config->sample_rate = 48000;
            break;
        }

        default:
            return -1;
    }

    return 0;
}

/* ================================================================
 *  Filter Expression Generation
 * ================================================================ */

int audio_build_filter_expression(
    AudioNormalizationMode mode,
    const PeakAnalysisResult *peak_result,
    const LoudnormAnalysisResult *loudnorm_result,
    char *output,
    size_t output_size)
{
    if (!output || output_size == 0) return -1;

    switch (mode) {
        case AUDIO_NORM_NONE:
            snprintf(output, output_size,
                     "aresample=resampler=soxr:precision=28:cheby=1");
            break;

        case AUDIO_NORM_PEAK:
            snprintf(output, output_size,
                     "aresample=resampler=soxr:precision=28:cheby=1,volume=-3dB");
            break;

        case AUDIO_NORM_PEAK_2PASS: {
            double gain = 0.0;
            if (peak_result && peak_result->valid) {
                gain = peak_result->gain;
            }
            snprintf(output, output_size,
                     "aresample=resampler=soxr:precision=28:cheby=1,volume=%.2fdB",
                     gain);
            break;
        }

        case AUDIO_NORM_LOUDNESS:
            snprintf(output, output_size,
                     "aresample=resampler=soxr:precision=28:cheby=1,"
                     "loudnorm=I=-11:TP=-1.5:LRA=7");
            break;

        case AUDIO_NORM_LOUDNESS_2PASS: {
            double I = -11, TP = -1.5, LRA = 7;
            double mI = 0, mTP = 0, mLRA = 0, mThresh = 0, mOffset = 0;
            if (loudnorm_result && loudnorm_result->valid) {
                I = loudnorm_result->I_target;
                TP = loudnorm_result->TP_target;
                LRA = loudnorm_result->LRA_target;
                mI = loudnorm_result->measured_I;
                mTP = loudnorm_result->measured_TP;
                mLRA = loudnorm_result->measured_LRA;
                mThresh = loudnorm_result->measured_thresh;
                mOffset = loudnorm_result->target_offset;
            }
            snprintf(output, output_size,
                     "aresample=resampler=soxr:precision=28:cheby=1,"
                     "loudnorm=I=%.1f:TP=%.1f:LRA=%.1f:"
                     "measured_I=%.2f:measured_TP=%.2f:measured_LRA=%.2f:"
                     "measured_thresh=%.2f:offset=%.2f:linear=true",
                     I, TP, LRA,
                     mI, mTP, mLRA,
                     mThresh, mOffset);
            break;
        }

        default:
            snprintf(output, output_size,
                     "aresample=resampler=soxr:precision=28:cheby=1");
            break;
    }

    return 0;
}

/* ================================================================
 *  Static helpers for analysis functions
 * ================================================================ */

static double parse_time_hms(const char *s) {
    int h = 0, m = 0;
    double sec = 0.0;
    if (sscanf(s, "%d:%d:%lf", &h, &m, &sec) == 3) {
        return h * 3600.0 + m * 60.0 + sec;
    }
    return 0.0;
}

static double json_number_or_string_value(const json_t *v) {
    if (!v) return 0.0;
    if (json_is_number(v)) return json_number_value(v);
    if (json_is_string(v)) return atof(json_string_value(v));
    return 0.0;
}

/* ================================================================
 *  Peak two-pass analysis
 * ================================================================ */

ConverterError audio_peak_analyze(
    const char *input_path,
    const ConverterCallbacks *callbacks,
    int *stop_flag,
    PeakAnalysisResult *out_result)
{
    if (!input_path || !out_result) return ERR_INVALID_OPTIONS;

    if (callbacks && callbacks->on_stage)
        callbacks->on_stage("peak analysis");

    const char *ffmpeg_bin = platform_get_ffmpeg_bin();
    int filter_threads = get_filter_threads();

    char *esc_ffmpeg = platform_escape_path_for_command(ffmpeg_bin);
    char *esc_input  = platform_escape_path_for_command(input_path);
    if (!esc_ffmpeg || !esc_input) {
        free(esc_ffmpeg);
        free(esc_input);
        if (callbacks && callbacks->on_error)
            callbacks->on_error("out of memory", ERR_UNKNOWN);
        return ERR_UNKNOWN;
    }

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
        "%s -hwaccel none -filter_threads %d -vn -i %s -af volumedetect -f null - 2>&1",
        esc_ffmpeg, filter_threads, esc_input);

    free(esc_ffmpeg);
    free(esc_input);

    /* --- duration for progress --- */
    const char *ffprobe_bin = platform_get_ffprobe_bin();
    char *esc_ffprobe = platform_escape_path_for_command(ffprobe_bin);
    char *esc_input2  = platform_escape_path_for_command(input_path);
    double duration = 0.0;
    if (esc_ffprobe && esc_input2) {
        char dcmd[4096];
        snprintf(dcmd, sizeof(dcmd),
                 "%s -v error -show_entries format=duration "
                 "-of default=noprint_wrappers=1:nokey=1 %s 2>%s",
                 esc_ffprobe, esc_input2, platform_get_null_device());
        FILE *dfp = platform_popen(dcmd, "r");
        if (dfp) {
            char dbuf[256];
            if (fgets(dbuf, sizeof(dbuf), dfp)) {
                duration = atof(dbuf);
            }
            platform_pclose(dfp);
        }
        free(esc_ffprobe);
        free(esc_input2);
    }

    FILE *fp = platform_popen(cmd, "r");
    if (!fp) {
        if (callbacks && callbacks->on_error)
            callbacks->on_error("popen failed", ERR_POPEN_FAILED);
        return ERR_POPEN_FAILED;
    }

    char line[512];
    double maxv = 0.0;
    double last_time = 0.0;
    double start_ts = (double)time(NULL);

    while (fgets(line, sizeof(line), fp)) {

        /* --- progress --- */
        if (duration > 0) {
            char *tpos = strstr(line, "time=");
            if (tpos) {
                tpos += 5;
                double cur = parse_time_hms(tpos);
                if (cur >= last_time) last_time = cur;

                double percent = (cur / duration) * 100.0;
                double elapsed = (double)time(NULL) - start_ts;
                double eta = (percent > 0)
                    ? elapsed * (100.0 - percent) / percent
                    : 0.0;

                if (callbacks && callbacks->on_progress_analysis)
                    callbacks->on_progress_analysis((float)percent, (float)eta);
            }
        }

        /* --- max_volume --- */
        if (strstr(line, "max_volume:")) {
            char *p = strstr(line, "max_volume:");
            if (p) {
                p += strlen("max_volume:");
                maxv = strtod(p, NULL);
            }
        }

        if (stop_flag && *stop_flag) {
            platform_pclose(fp);
            return ERR_SKIP_FILE;
        }
    }

    int status = platform_pclose(fp);
    if (status != 0) {
        if (callbacks && callbacks->on_error)
            callbacks->on_error("peak analysis failed", ERR_PEAK_ANALYSIS_FAILED);
        return ERR_PEAK_ANALYSIS_FAILED;
    }

    /* target = -3 dB */
    double target = -3.0;
    double gain = target - maxv;

    out_result->max_volume = maxv;
    out_result->gain = gain;
    out_result->valid = 1;

    return ERR_OK;
}

/* ================================================================
 *  Loudnorm two-pass analysis
 * ================================================================ */

ConverterError audio_loudnorm_analyze(
    const char *input_path,
    AudioGenre genre,
    const ConverterCallbacks *callbacks,
    int *stop_flag,
    LoudnormAnalysisResult *out_result)
{
    if (!input_path || !out_result) return ERR_INVALID_OPTIONS;

    double I_target, TP_target, LRA_target;
    if (!audio_genre_get_targets(genre, &I_target, &TP_target, &LRA_target)) {
        /* Fallback to default targets */
        I_target  = -11;
        TP_target = -1.5;
        LRA_target = 7;
    }

    if (callbacks && callbacks->on_stage)
        callbacks->on_stage("loudnorm analysis");

    const char *ffmpeg_bin = platform_get_ffmpeg_bin();
    int filter_threads = get_filter_threads();

    char *esc_ffmpeg = platform_escape_path_for_command(ffmpeg_bin);
    char *esc_input  = platform_escape_path_for_command(input_path);
    if (!esc_ffmpeg || !esc_input) {
        free(esc_ffmpeg);
        free(esc_input);
        if (callbacks && callbacks->on_error)
            callbacks->on_error("out of memory", ERR_UNKNOWN);
        return ERR_UNKNOWN;
    }

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
        "%s -hwaccel none -filter_threads %d -vn -i %s -af "
        "\"loudnorm=I=%.1f:TP=%.1f:LRA=%.1f:linear=true:print_format=json\" "
        "-f null - 2>&1",
        esc_ffmpeg, filter_threads, esc_input, I_target, TP_target, LRA_target);

    free(esc_ffmpeg);
    free(esc_input);

    /* --- duration for progress --- */
    const char *ffprobe_bin = platform_get_ffprobe_bin();
    char *esc_ffprobe = platform_escape_path_for_command(ffprobe_bin);
    char *esc_input2  = platform_escape_path_for_command(input_path);
    double duration = 0.0;
    if (esc_ffprobe && esc_input2) {
        char dcmd[4096];
        snprintf(dcmd, sizeof(dcmd),
                 "%s -v error -show_entries format=duration "
                 "-of default=noprint_wrappers=1:nokey=1 %s 2>%s",
                 esc_ffprobe, esc_input2, platform_get_null_device());
        FILE *dfp = platform_popen(dcmd, "r");
        if (dfp) {
            char dbuf[256];
            if (fgets(dbuf, sizeof(dbuf), dfp)) {
                duration = atof(dbuf);
            }
            platform_pclose(dfp);
        }
        free(esc_ffprobe);
        free(esc_input2);
    }

    FILE *fp = platform_popen(cmd, "r");
    if (!fp) {
        if (callbacks && callbacks->on_error)
            callbacks->on_error("popen failed", ERR_POPEN_FAILED);
        return ERR_POPEN_FAILED;
    }

    char buf[131072];
    size_t pos = 0;
    char line[1024];
    double last_time = 0.0;
    double start_ts = (double)time(NULL);

    while (fgets(line, sizeof(line), fp)) {

        /* --- progress --- */
        if (duration > 0) {
            char *tpos = strstr(line, "time=");
            if (tpos) {
                tpos += 5;
                double cur = parse_time_hms(tpos);
                if (cur >= last_time) last_time = cur;

                double percent = (cur / duration) * 100.0;
                double elapsed = (double)time(NULL) - start_ts;
                double eta = (percent > 0)
                    ? elapsed * (100.0 - percent) / percent
                    : 0.0;

                if (callbacks && callbacks->on_progress_analysis)
                    callbacks->on_progress_analysis((float)percent, (float)eta);
            }
        }

        /* --- collect JSON --- */
        size_t len = strlen(line);
        if (pos + len < sizeof(buf) - 1) {
            memcpy(buf + pos, line, len);
            pos += len;
        }

        if (stop_flag && *stop_flag) {
            platform_pclose(fp);
            return ERR_SKIP_FILE;
        }
    }

    buf[pos] = '\0';

    int status = platform_pclose(fp);
    if (status != 0) {
        if (callbacks && callbacks->on_error)
            callbacks->on_error("loudnorm analysis failed", ERR_LOUDNORM_ANALYSIS_FAILED);
        return ERR_LOUDNORM_ANALYSIS_FAILED;
    }

    /* --- extract JSON --- */
    char *start = strrchr(buf, '{');
    char *end   = strrchr(buf, '}');

    if (!start || !end || end < start) {
        if (callbacks && callbacks->on_error)
            callbacks->on_error("invalid loudnorm JSON", ERR_LOUDNORM_ANALYSIS_FAILED);
        return ERR_LOUDNORM_ANALYSIS_FAILED;
    }

    end[1] = '\0';

    json_error_t err;
    json_t *root = json_loads(start, 0, &err);
    if (!root) {
        if (callbacks && callbacks->on_error)
            callbacks->on_error("JSON parse failed", ERR_LOUDNORM_ANALYSIS_FAILED);
        return ERR_LOUDNORM_ANALYSIS_FAILED;
    }

    double I      = json_number_or_string_value(json_object_get(root, "input_i"));
    double TP     = json_number_or_string_value(json_object_get(root, "input_tp"));
    double LRA    = json_number_or_string_value(json_object_get(root, "input_lra"));
    double thresh = json_number_or_string_value(json_object_get(root, "input_thresh"));
    double offset = json_number_or_string_value(json_object_get(root, "target_offset"));

    json_decref(root);

    out_result->I_target = I_target;
    out_result->TP_target = TP_target;
    out_result->LRA_target = LRA_target;
    out_result->measured_I = I;
    out_result->measured_TP = TP;
    out_result->measured_LRA = LRA;
    out_result->measured_thresh = thresh;
    out_result->target_offset = offset;
    out_result->valid = 1;

    return ERR_OK;
}
