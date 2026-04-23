#include "converter.h"
#include "converter_platform.h"
#include "converter_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <jansson.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

struct Converter {
    ConvertOptions opts;
    ConverterCallbacks cb;
    int stop_flag;

    /* Platform state */
    int platform_initialized;  /* 1 after successful platform_init() */
    int platform_caps;         /* PLAT_CAP_* bitmask from platform_detect_gpu_support() */
};

static int codec_is_vaapi(const char* codec) {
    return codec &&
           (strcmp(codec, "h264_vaapi") == 0 ||
            strcmp(codec, "hevc_vaapi") == 0);
}

static int codec_uses_mov_container(const char* codec) {
    return codec &&
           (strcmp(codec, "prores") == 0 ||
            strcmp(codec, "prores_ks") == 0 ||
            strcmp(codec, "prores_videotoolbox") == 0);
}

static int audio_output_mode_is(const char* mode, const char* expected) {
    return mode && expected && strcmp(mode, expected) == 0;
}

static int audio_output_mode_valid(const char* mode) {
    return mode[0] == '\0' ||
           audio_output_mode_is(mode, "pcm") ||
           audio_output_mode_is(mode, "fdk_aac_q5") ||
           audio_output_mode_is(mode, "fdk_aac_q5_ac3_640") ||
           audio_output_mode_is(mode, "fdk_aac_q2") ||
           audio_output_mode_is(mode, "fdk_aac_q2_ac3_640");
}

static void build_audio_filter_expr(const ConvertOptions* opts, char* filter, size_t filter_sz) {
    if (!filter || filter_sz == 0) {
        return;
    }

    if (strcmp(opts->audio_norm, "none") == 0) {
        snprintf(filter, filter_sz, "aresample=resampler=soxr:precision=28:cheby=1");
    }
    else if (strcmp(opts->audio_norm, "peak_norm") == 0) {
        snprintf(filter, filter_sz,
                 "aresample=resampler=soxr:precision=28:cheby=1,volume=-3dB");
    }
    else if (strcmp(opts->audio_norm, "peak_norm_2pass") == 0) {
        snprintf(filter, filter_sz,
                 "aresample=resampler=soxr:precision=28:cheby=1,volume=%.2fdB",
                 opts->gain);
    }
    else if (strcmp(opts->audio_norm, "loudness_norm") == 0) {
        snprintf(filter, filter_sz,
                 "aresample=resampler=soxr:precision=28:cheby=1,"
                 "loudnorm=I=-11:TP=-1.5:LRA=7");
    }
    else if (strcmp(opts->audio_norm, "loudness_norm_2pass") == 0) {
        snprintf(filter, filter_sz,
                 "aresample=resampler=soxr:precision=28:cheby=1,"
                 "loudnorm=I=%.1f:TP=%.1f:LRA=%.1f:"
                 "measured_I=%.2f:measured_TP=%.2f:measured_LRA=%.2f:"
                 "measured_thresh=%.2f:offset=%.2f:linear=true",
                 opts->I_target,
                 opts->TP_target,
                 opts->LRA_target,
                 opts->measured_I,
                 opts->measured_TP,
                 opts->measured_LRA,
                 opts->measured_thresh,
                 opts->measured_offset);
    }
    else {
        snprintf(filter, filter_sz, "aresample=resampler=soxr:precision=28:cheby=1");
    }
}

static int codec_uses_aac_audio(const char* codec) {
    return codec &&
           (strcmp(codec, "hevc_videotoolbox") == 0);
}

static const char* get_ffmpeg_bin(void);

static int ffmpeg_encoder_available(const char* encoder_name) {
    static int initialized = 0;
    static int has_aac_at = 0;
    static int has_libfdk_aac = 0;
    static int has_aac = 0;

    if (!encoder_name || encoder_name[0] == '\0') {
        return 0;
    }

    if (!initialized) {
        const char* ffmpeg_bin = platform_get_ffmpeg_bin();
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
                 "\"%s\" -hide_banner -v error -encoders 2>%s",
                 ffmpeg_bin, platform_get_null_device());

        FILE* fp = popen(cmd, "r");
        if (fp) {
            char line[1024];
            while (fgets(line, sizeof(line), fp)) {
                if (!has_aac_at && strstr(line, " aac_at")) {
                    has_aac_at = 1;
                }
                if (!has_libfdk_aac && strstr(line, " libfdk_aac")) {
                    has_libfdk_aac = 1;
                }
                if (!has_aac && strstr(line, " aac ")) {
                    has_aac = 1;
                }
            }
            pclose(fp);
        }

        initialized = 1;
    }

    if (strcmp(encoder_name, "aac_at") == 0) {
        return has_aac_at;
    }
    if (strcmp(encoder_name, "libfdk_aac") == 0) {
        return has_libfdk_aac;
    }
    if (strcmp(encoder_name, "aac") == 0) {
        return has_aac;
    }

    return 0;
}

static const char* get_ffmpeg_bin(void) {
    return platform_get_ffmpeg_bin();
}

static const char* get_ffprobe_bin(void) {
    return platform_get_ffprobe_bin();
}

// ------------------------------------------------------------
//  Create / Destroy
// ------------------------------------------------------------
Converter* converter_create(void) {
    Converter* c = calloc(1, sizeof(Converter));
    if (!c) return NULL;
    if (platform_init() != 0) {
        free(c);
        return NULL;
    }
    c->platform_initialized = 1;
    c->platform_caps = platform_detect_gpu_support();
    return c;
}

void converter_destroy(Converter* c) {
    if (!c) return;
    if (c->platform_initialized)
        platform_cleanup();
    free(c);
}

// ------------------------------------------------------------
//  Set Callbacks
// ------------------------------------------------------------
void converter_set_callbacks(
    Converter* c,
    const ConverterCallbacks* cb
) {
    if (!c) return;
    if (cb)
        c->cb = *cb;
    else
        memset(&c->cb, 0, sizeof(c->cb));
}

// ------------------------------------------------------------
//  Set Options
// ------------------------------------------------------------
ConverterError converter_set_options(
    Converter* c,
    const ConvertOptions* opts
) {
    if (!c || !opts)
        return ERR_INVALID_OPTIONS;

    c->opts = *opts;

    if (!audio_output_mode_valid(c->opts.audio_output_mode))
        return ERR_INVALID_OPTIONS;

    /* Validate required audio filters */
    if (!platform_validate_audio_filters()) {
        if (c->cb.on_error)
            c->cb.on_error("required FFmpeg audio filters not available",
                           ERR_AUDIO_FILTER_VALIDATION_FAILED);
        return ERR_AUDIO_FILTER_VALIDATION_FAILED;
    }

    /* Platform-specific codec validation */
    if (c->opts.codec[0] != '\0' && !platform_supports_codec(c->opts.codec)) {
        if (c->cb.on_error)
            c->cb.on_error("requested codec not supported on this platform",
                           ERR_INVALID_OPTIONS);
        return ERR_INVALID_OPTIONS;
    }

    /* For VAAPI codecs: fill hw_device if not already set by the caller */
    if (codec_is_vaapi(c->opts.codec)) {
        if (c->opts.hw_device[0] == '\0') {
            platform_get_hw_device_for_codec(c->opts.codec,
                                              c->opts.hw_device,
                                              sizeof(c->opts.hw_device));
        }
        if (c->opts.hw_device[0] == '\0')
            return ERR_INVALID_OPTIONS;
        c->opts.hwaccel_enabled = 1;
    }

    return ERR_OK;
}

// ------------------------------------------------------------
//  Stop
// ------------------------------------------------------------
void converter_stop(Converter* c) {
    if (!c) return;
    c->stop_flag = 1;
}

// ------------------------------------------------------------
//  Error String
// ------------------------------------------------------------
const char* converter_error_string(ConverterError err) {
    switch (err) {
        case ERR_OK: return "OK";
        case ERR_INPUT_NOT_FOUND: return "input file not found";
        case ERR_INPUT_NOT_REGULAR: return "input file is not a regular file";
        case ERR_INPUT_NOT_READABLE: return "input file not readable";
        case ERR_OUTPUT_EXISTS: return "output file exists";
        case ERR_SKIP_FILE: return "file skipped";
        case ERR_PEAK_ANALYSIS_FAILED: return "peak analysis failed";
        case ERR_LOUDNORM_ANALYSIS_FAILED: return "loudnorm analysis failed";
        case ERR_FFMPEG_FAILED: return "ffmpeg failed";
        case ERR_FFPROBE_FAILED: return "ffprobe failed";
        case ERR_POPEN_FAILED: return "popen failed";
        case ERR_PCLOSE_FAILED: return "pclose failed";
        case ERR_INVALID_OPTIONS: return "invalid options";
        case ERR_PLATFORM_INIT_FAILED: return "platform initialization failed";
        case ERR_AUDIO_FILTER_VALIDATION_FAILED: return "required FFmpeg audio filter not available";
        case ERR_GPU_NOT_SUPPORTED: return "GPU codec not supported on this platform";
        case ERR_PATH_TOO_LONG: return "path exceeds maximum length";
        case ERR_HOME_DIR_NOT_FOUND: return "user home directory not found";
        default: return "unknown error";
    }
}

// ------------------------------------------------------------
//  Helpers: time parsing (retained for immutable audio functions)
// ------------------------------------------------------------
static double parse_time_hms(const char *s) {
    int h = 0, m = 0;
    double sec = 0.0;
    if (sscanf(s, "%d:%d:%lf", &h, &m, &sec) == 3) {
        return h * 3600.0 + m * 60.0 + sec;
    }
    return 0.0;
}

// ------------------------------------------------------------
//  Output dir preflight
// ------------------------------------------------------------
static ConverterError ensure_output_dir_writable(
    Converter* c,
    const ConvertOptions* opts,
    char* out_dir,
    size_t out_dir_sz
) {
    if (!opts || !out_dir || out_dir_sz == 0)
        return ERR_INVALID_OPTIONS;

    const char* configured = opts->output_dir;
    if (!configured || configured[0] == '\0') {
        const char* home = platform_get_home_dir();
        char* joined = platform_join_paths(home, "ffmpeg_converter");
        if (joined) {
            strncpy(out_dir, joined, out_dir_sz - 1);
            out_dir[out_dir_sz - 1] = '\0';
            free(joined);
        } else {
            strncpy(out_dir, "ffmpeg_converter", out_dir_sz - 1);
            out_dir[out_dir_sz - 1] = '\0';
        }
    } else {
        strncpy(out_dir, configured, out_dir_sz - 1);
        out_dir[out_dir_sz - 1] = '\0';
    }

    if (platform_mkdir_recursive(out_dir) != 0) {
        if (c->cb.on_error)
            c->cb.on_error("output preflight failed: cannot create output directory", ERR_INVALID_OPTIONS);
        return ERR_INVALID_OPTIONS;
    }

    struct stat st;
    if (stat(out_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        if (c->cb.on_error)
            c->cb.on_error("output preflight failed: output path is not a directory", ERR_INVALID_OPTIONS);
        return ERR_INVALID_OPTIONS;
    }

    if (!platform_is_dir_writable(out_dir)) {
        if (c->cb.on_error)
            c->cb.on_error("output preflight failed: output directory not writable", ERR_INVALID_OPTIONS);
        return ERR_INVALID_OPTIONS;
    }

    return ERR_OK;
}

// ------------------------------------------------------------
//  ffprobe duration
// ------------------------------------------------------------
static double get_duration(const char *input) {
    char cmd[2048];
    const char *ffprobe_bin = get_ffprobe_bin();
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -v error -show_entries format=duration "
             "-of default=noprint_wrappers=1:nokey=1 \"%s\" 2>%s",
             ffprobe_bin, input, platform_get_null_device());

    FILE *fp = popen(cmd, "r");
    if (!fp) return 0.0;

    char buf[256];
    if (!fgets(buf, sizeof(buf), fp)) {
        pclose(fp);
        return 0.0;
    }
    pclose(fp);

    return atof(buf);
}

// ------------------------------------------------------------
//  File checks
// ------------------------------------------------------------
static ConverterError check_file(Converter* c, const char *file) {
    struct stat st;

    if (stat(file, &st) != 0) {
        if (c->cb.on_error)
            c->cb.on_error("input file not found", ERR_INPUT_NOT_FOUND);
        return ERR_INPUT_NOT_FOUND;
    }

    if (!S_ISREG(st.st_mode)) {
        if (c->cb.on_error)
            c->cb.on_error("input file is not a regular file", ERR_INPUT_NOT_REGULAR);
        return ERR_INPUT_NOT_REGULAR;
    }

    if (!platform_is_file_readable(file)) {
        if (c->cb.on_error)
            c->cb.on_error("input file not readable", ERR_INPUT_NOT_READABLE);
        return ERR_INPUT_NOT_READABLE;
    }

    return ERR_OK;
}

// ------------------------------------------------------------
//  Output name generation (with basename + optional output_dir)
// ------------------------------------------------------------
static void make_output_name(
    const char* input,
    const ConvertOptions* opts,
    char* out,
    size_t out_sz
) {
    if (out_sz == 0) return;

    // 1. basename — platform handles separator differences
    const char* name = platform_get_filename(input);

    // 2. base without extension
    char base[512];
    size_t name_len = strlen(name);
    size_t copy_len = (name_len < sizeof(base) - 1) ? name_len : sizeof(base) - 1;
    strncpy(base, name, copy_len);
    base[copy_len] = '\0';

    // Remove extension
    char* dot = strrchr(base, '.');
    if (dot) *dot = '\0';

    // 3. Build new filename with safe length
    char filename[1024];
    size_t base_len = strlen(base);

    // Maximum base length to fit "_converted.ext\0" (15 chars)
    size_t max_safe_base_len = sizeof(filename) - 15;

    if (base_len > max_safe_base_len) {
        /* Truncate base to fit */
        base[max_safe_base_len] = '\0';
    }

    const char *ext;
    if (strcmp(opts->codec, "copy") == 0)
        ext = "mkv";
    else if (strcmp(opts->codec, "hevc_videotoolbox") == 0)
        ext = "mp4";
    else if (codec_uses_mov_container(opts->codec))
        ext = "mov";
    else
        ext = "mkv";
    snprintf(filename, sizeof(filename), "%s_converted.%s", base, ext);

    // 4. If output_dir is not specified
    if (opts->output_dir[0] == '\0') {
        strncpy(out, filename, out_sz - 1);
        out[out_sz - 1] = '\0';
        return;
    }

    // 5. With output_dir — use platform_join_paths for correct separator
    char* joined = platform_join_paths(opts->output_dir, filename);
    if (joined) {
        strncpy(out, joined, out_sz - 1);
        out[out_sz - 1] = '\0';
        free(joined);
    } else {
        /* Allocation failed — fall back to filename only */
        strncpy(out, filename, out_sz - 1);
        out[out_sz - 1] = '\0';
    }
}

void converter_make_output_name(
    const char* input,
    const ConvertOptions* opts,
    char* out,
    size_t out_sz
) {
    make_output_name(input, opts, out, out_sz);
}
// ------------------------------------------------------------
//  Output file existence check
// ------------------------------------------------------------
static ConverterError check_output_exists(
    Converter* c,
    const char* output
) {
    struct stat st;
    if (stat(output, &st) == 0) {
        // file exists
        if (c->opts.overwrite == 0) {
            if (c->cb.on_message)
                c->cb.on_message("output file exists — skipping");

            return ERR_OUTPUT_EXISTS;
        }
    }
    return ERR_OK;
}

// ------------------------------------------------------------
//  Peak 2-pass analysis
// ------------------------------------------------------------
static ConverterError peak_two_pass(
    Converter* c,
    const char* input,
    double* out_gain
) {
    if (c->cb.on_stage)
        c->cb.on_stage("peak analysis");

    char cmd[2048];
    const char *ffmpeg_bin = get_ffmpeg_bin();
    int filter_threads = get_filter_threads();
    snprintf(cmd, sizeof(cmd),
        "\"%s\" -filter_threads %d -vn -i \"%s\" -af volumedetect -f null - 2>&1",
        ffmpeg_bin, filter_threads, input);

    double duration = get_duration(input);
    FILE* fp = popen(cmd, "r");
    if (!fp) {
        if (c->cb.on_error)
            c->cb.on_error("popen failed", ERR_POPEN_FAILED);
        return ERR_POPEN_FAILED;
    }

    char line[512];
    double maxv = 0.0;
    double last_time = 0.0;
    double start_ts = (double)time(NULL);

    while (fgets(line, sizeof(line), fp)) {

        // --- progress ---
        if (duration > 0) {
            char* tpos = strstr(line, "time=");
            if (tpos) {
                tpos += 5;
                double cur = parse_time_hms(tpos);
                if (cur >= last_time) last_time = cur;

                double percent = (cur / duration) * 100.0;
                double elapsed = (double)time(NULL) - start_ts;
                double eta = (percent > 0)
                    ? elapsed * (100.0 - percent) / percent
                    : 0.0;

                if (c->cb.on_progress_analysis)
                    c->cb.on_progress_analysis((float)percent, (float)eta);
            }
        }

        // --- max_volume ---
        if (strstr(line, "max_volume:")) {
            char* p = strstr(line, "max_volume:");
            if (p) {
                p += strlen("max_volume:");
                maxv = strtod(p, NULL);
            }
        }

        if (c->stop_flag) {
            pclose(fp);
            return ERR_SKIP_FILE;
        }
    }

    int status = pclose(fp);
    if (status != 0) {
        if (c->cb.on_error)
            c->cb.on_error("peak analysis failed", ERR_PEAK_ANALYSIS_FAILED);
        return ERR_PEAK_ANALYSIS_FAILED;
    }

    // target = -3 dB
    double target = -3.0;
    *out_gain = target - maxv;

    return ERR_OK;
}

// ------------------------------------------------------------
//  Loudnorm 2-pass analysis
// ------------------------------------------------------------
static double json_number_or_string_value(const json_t *v) {
    if (!v) return 0.0;
    if (json_is_number(v)) return json_number_value(v);
    if (json_is_string(v)) return atof(json_string_value(v));
    return 0.0;
}

static ConverterError loudnorm_two_pass(
    Converter* c,
    const char* input,
    double I_target,
    double TP_target,
    double LRA_target,
    double* I,
    double* TP,
    double* LRA,
    double* thresh,
    double* offset
) {
    if (c->cb.on_stage)
        c->cb.on_stage("loudnorm analysis");

    char cmd[2048];
    const char *ffmpeg_bin = get_ffmpeg_bin();
    int filter_threads = get_filter_threads();
    snprintf(cmd, sizeof(cmd),
        "\"%s\" -filter_threads %d -vn -i \"%s\" -af "
        "\"loudnorm=I=%.1f:TP=%.1f:LRA=%.1f:linear=true:print_format=json\" "
        "-f null - 2>&1",
        ffmpeg_bin, filter_threads, input, I_target, TP_target, LRA_target);

    double duration = get_duration(input);
    FILE* fp = popen(cmd, "r");
    if (!fp) {
        if (c->cb.on_error)
            c->cb.on_error("popen failed", ERR_POPEN_FAILED);
        return ERR_POPEN_FAILED;
    }

    char buf[131072];
    size_t pos = 0;
    char line[1024];
    double last_time = 0.0;
    double start_ts = (double)time(NULL);

    while (fgets(line, sizeof(line), fp)) {

        // --- progress ---
        if (duration > 0) {
            char* tpos = strstr(line, "time=");
            if (tpos) {
                tpos += 5;
                double cur = parse_time_hms(tpos);
                if (cur >= last_time) last_time = cur;

                double percent = (cur / duration) * 100.0;
                double elapsed = (double)time(NULL) - start_ts;
                double eta = (percent > 0)
                    ? elapsed * (100.0 - percent) / percent
                    : 0.0;

                if (c->cb.on_progress_analysis)
                    c->cb.on_progress_analysis((float)percent, (float)eta);
            }
        }

        // --- collect JSON ---
        size_t len = strlen(line);
        if (pos + len < sizeof(buf) - 1) {
            memcpy(buf + pos, line, len);
            pos += len;
        }

        if (c->stop_flag) {
            pclose(fp);
            return ERR_SKIP_FILE;
        }
    }

    buf[pos] = 0;
    int status = pclose(fp);
    if (status != 0) {
        if (c->cb.on_error)
            c->cb.on_error("loudnorm analysis failed", ERR_LOUDNORM_ANALYSIS_FAILED);
        return ERR_LOUDNORM_ANALYSIS_FAILED;
    }

    // --- extract JSON ---
    char* start = strrchr(buf, '{');
    char* end   = strrchr(buf, '}');

    if (!start || !end || end < start) {
        if (c->cb.on_error)
            c->cb.on_error("invalid loudnorm JSON", ERR_LOUDNORM_ANALYSIS_FAILED);
        return ERR_LOUDNORM_ANALYSIS_FAILED;
    }

    end[1] = 0;

    json_error_t err;
    json_t* root = json_loads(start, 0, &err);
    if (!root) {
        if (c->cb.on_error)
            c->cb.on_error("JSON parse failed", ERR_LOUDNORM_ANALYSIS_FAILED);
        return ERR_LOUDNORM_ANALYSIS_FAILED;
    }

    *I      = json_number_or_string_value(json_object_get(root, "input_i"));
    *TP     = json_number_or_string_value(json_object_get(root, "input_tp"));
    *LRA    = json_number_or_string_value(json_object_get(root, "input_lra"));
    *thresh = json_number_or_string_value(json_object_get(root, "input_thresh"));
    *offset = json_number_or_string_value(json_object_get(root, "target_offset"));

    json_decref(root);
    return ERR_OK;
}

// ------------------------------------------------------------
//  Build ffmpeg command (strictly same logic as CLI)
// ------------------------------------------------------------
static void build_ffmpeg_cmd(
    Converter* c,
    const char* input,
    const char* output,
    char* cmd_out,
    size_t cmd_out_sz
) {
    const ConvertOptions* opts = &c->opts;
    const char* ffmpeg_bin = get_ffmpeg_bin();
    int is_dual_audio_output =
        audio_output_mode_is(opts->audio_output_mode, "fdk_aac_q5_ac3_640") ||
        audio_output_mode_is(opts->audio_output_mode, "fdk_aac_q2_ac3_640");
    int is_fdk_single_audio_output =
        audio_output_mode_is(opts->audio_output_mode, "fdk_aac_q5") ||
        audio_output_mode_is(opts->audio_output_mode, "fdk_aac_q2");
    int prefer_fdk_q2 =
        audio_output_mode_is(opts->audio_output_mode, "fdk_aac_q2") ||
        audio_output_mode_is(opts->audio_output_mode, "fdk_aac_q2_ac3_640");
    int fdk_vbr = prefer_fdk_q2 ? 2 : 5;
    int has_aac_at = ffmpeg_encoder_available("aac_at");
    int has_libfdk_aac = ffmpeg_encoder_available("libfdk_aac");
    int has_native_aac = ffmpeg_encoder_available("aac");
    char audio_filter[1024];

    char cmd[16384];
    cmd[0] = 0;

    build_audio_filter_expr(opts, audio_filter, sizeof(audio_filter));

    snprintf(cmd, sizeof(cmd), "\"%s\" ", ffmpeg_bin);

    if (opts->overwrite)
        strcat(cmd, "-y ");
    else
        strcat(cmd, "-n ");

    /* VAAPI requires a device node before the input */
    if (opts->hwaccel_enabled && opts->hw_device[0] != '\0') {
        strcat(cmd, "-vaapi_device ");
        strcat(cmd, "\"");
        strcat(cmd, opts->hw_device);
        strcat(cmd, "\" ");
    }

    strcat(cmd, "-i ");
    strcat(cmd, "\"");
    strcat(cmd, input);
    strcat(cmd, "\" ");

    // map
    strcat(cmd, "-map 0:v:0 ");
    if (is_dual_audio_output) {
        strcat(cmd, "-filter_complex \"[0:a:0]");
        strcat(cmd, audio_filter);
        strcat(cmd, ",asplit=2[aout0][aout1]\" ");
        strcat(cmd, "-map [aout0] -map [aout1] ");
    } else {
        strcat(cmd, "-map 0:a:0 ");
    }
    strcat(cmd, "-map_metadata 0 ");

    // video codec
    // Try platform-specific codec flags first (VAAPI, VideoToolbox, NVENC, etc.)
    const char* platform_vcodec = platform_get_video_codec_flags(opts->codec, input, opts);
    if (platform_vcodec != NULL) {
        strcat(cmd, platform_vcodec);
    }
    else if (strcmp(opts->codec, "prores") == 0 ||
             strcmp(opts->codec, "prores_ks") == 0)
    {
        int profile_value = opts->profile;
        if (profile_value < 1 || profile_value > 4) {
            profile_value = 2; // standard
        }

        if (strcmp(opts->codec, "prores_ks") == 0) {
            const char* profile_name = "standard";
            if (profile_value == 1) profile_name = "lt";
            else if (profile_value == 3) profile_name = "hq";
            else if (profile_value == 4) profile_name = "4444";

            char tmp[160];
            snprintf(tmp, sizeof(tmp),
                     "-c:v prores_ks -profile:v %s ",
                     profile_name);
            strcat(cmd, tmp);
        } else {
            char tmp[128];
            snprintf(tmp, sizeof(tmp),
                     "-c:v prores -profile:v %d ",
                     profile_value);
            strcat(cmd, tmp);
        }
    }
    else {
        strcat(cmd, "-c:v copy ");
    }

    // deblock (not applicable for hardware encoders or hwaccel-enabled codecs)
    if (platform_vcodec == NULL && !opts->hwaccel_enabled) {
        if (opts->deblock == 2) {
            strcat(cmd,
                "-vf \"deblock=filter=weak:block=4:planes=1\" ");
        }
        else if (opts->deblock == 3) {
            strcat(cmd,
                "-vf \"deblock=filter=strong:block=4:"
                "alpha=0.12:beta=0.07:gamma=0.06:delta=0.05:planes=1\" ");
        }
    }
    else if (opts->hwaccel_enabled) {
        /* VAAPI requires pixel format conversion and GPU upload */
        strcat(cmd, "-vf \"format=nv12,hwupload\" ");
    }

    // audio codec
    if (is_dual_audio_output) {
        if (has_aac_at) {
            strcat(cmd, "-c:a:0 aac_at -q:a:0 2 -ar:a:0 48000 ");
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: aac_at");
        } else if (has_libfdk_aac) {
            char aac0_opts[128];
            snprintf(aac0_opts, sizeof(aac0_opts),
                     "-c:a:0 libfdk_aac -vbr:a:0 %d -ar:a:0 48000 ",
                     fdk_vbr);
            strcat(cmd, aac0_opts);
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: libfdk_aac");
        } else if (has_native_aac) {
            strcat(cmd, "-c:a:0 aac -q:a:0 2 -ar:a:0 48000 ");
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: native aac");
        } else {
            strcat(cmd, "-c:a:0 aac -q:a:0 2 -ar:a:0 48000 ");
            if (c->cb.on_message) c->cb.on_message("AAC encoder fallback: native aac (unverified)");
        }
        strcat(cmd, "-c:a:1 ac3 -b:a:1 640k -ar:a:1 48000 ");
    } else if (is_fdk_single_audio_output) {
        if (has_aac_at) {
            strcat(cmd, "-c:a aac_at -q:a 2 -ar 48000 ");
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: aac_at");
        } else if (has_libfdk_aac) {
            char fdk_opts[128];
            snprintf(fdk_opts, sizeof(fdk_opts), "-c:a libfdk_aac -vbr %d -ar 48000 ", fdk_vbr);
            strcat(cmd, fdk_opts);
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: libfdk_aac");
        } else if (has_native_aac) {
            strcat(cmd, "-c:a aac -q:a 2 -ar 48000 ");
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: native aac");
        } else {
            strcat(cmd, "-c:a aac -q:a 2 -ar 48000 ");
            if (c->cb.on_message) c->cb.on_message("AAC encoder fallback: native aac (unverified)");
        }
    } else if (audio_output_mode_is(opts->audio_output_mode, "pcm")) {
        strcat(cmd, "-c:a pcm_s16le -ar 48000 ");
    } else if (c->opts.use_aac_for_h265) {
        if (has_aac_at) {
            strcat(cmd, "-c:a aac_at -q:a 2 -ar 48000 ");
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: aac_at");
        } else if (has_libfdk_aac) {
            char fdk_opts[128];
            snprintf(fdk_opts, sizeof(fdk_opts), "-c:a libfdk_aac -vbr %d -ar 48000 ", fdk_vbr);
            strcat(cmd, fdk_opts);
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: libfdk_aac");
        } else {
            strcat(cmd, "-c:a aac -q:a 2 -ar 48000 ");
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: native aac");
        }
    } else {
        strcat(cmd, "-c:a pcm_s16le -ar 48000 ");
    }

    if (!is_dual_audio_output) {
        strcat(cmd, "-af \"");
        strcat(cmd, audio_filter);
        strcat(cmd, "\" ");
    }

    // ffmpeg progress options must be placed before output
    strcat(cmd, "-progress pipe:1 -nostats -nostdin ");

    // output
    strcat(cmd, "\"");
    strcat(cmd, output);
    strcat(cmd, "\"");

    // copy to output buffer
    strncpy(cmd_out, cmd, cmd_out_sz);
    cmd_out[cmd_out_sz - 1] = 0;

    if (c->cb.on_message) {
        c->cb.on_message("ffmpeg command built");
    }
}

// ------------------------------------------------------------
//  FFmpeg encoding with progress
// ------------------------------------------------------------
static ConverterError run_ffmpeg_encode_with_progress(
    Converter* c,
    const char* cmd_base,
    double duration
) {
    if (c->cb.on_stage)
        c->cb.on_stage("encoding");

    /* Allocate a command buffer large enough for cmd_base + " 2>&1\0" */
    size_t base_len = strlen(cmd_base);
    char* cmd = malloc(base_len + 8);
    if (!cmd) {
        if (c->cb.on_error)
            c->cb.on_error("out of memory", ERR_UNKNOWN);
        return ERR_UNKNOWN;
    }
    snprintf(cmd, base_len + 8, "%s 2>&1", cmd_base);

    FILE* fp = popen(cmd, "r");
    free(cmd);
    if (!fp) {
        if (c->cb.on_error)
            c->cb.on_error("popen failed", ERR_POPEN_FAILED);
        return ERR_POPEN_FAILED;
    }

    char line[512];
    double out_time_ms = 0.0;
    double fps = 0.0;
    double start_ts = (double)time(NULL);

    while (fgets(line, sizeof(line), fp)) {
        platform_normalize_output_line(line);

        // out_time_ms
        if (strncmp(line, "out_time_ms=", 12) == 0) {
            out_time_ms = atof(line + 12);
        }
        // fps
        else if (strncmp(line, "fps=", 4) == 0) {
            fps = atof(line + 4);
        }
        // progress=end
        else if (strncmp(line, "progress=", 9) == 0) {
            if (strstr(line + 9, "end")) {
                if (duration > 0 && c->cb.on_progress_encode) {
                    c->cb.on_progress_encode(100.0f, (float)fps, 0.0f);
                }
                break;
            }
        }

        // progress update
        if (duration > 0 && out_time_ms > 0) {
            double cur = out_time_ms / 1000000.0;
            double percent = (cur / duration) * 100.0;
            double elapsed = (double)time(NULL) - start_ts;
            double eta = (percent > 0)
                ? elapsed * (100.0 - percent) / percent
                : 0.0;

            if (c->cb.on_progress_encode)
                c->cb.on_progress_encode(
                    (float)percent,
                    (float)fps,
                    (float)eta
                );
        }

        if (c->stop_flag) {
            pclose(fp);
            return ERR_SKIP_FILE;
        }
    }

    int status = pclose(fp);
    if (status != 0) {
        if (c->cb.on_error)
            c->cb.on_error("ffmpeg failed", ERR_FFMPEG_FAILED);
        return ERR_FFMPEG_FAILED;
    }

    if (c->cb.on_message)
        c->cb.on_message("encoding finished");

    return ERR_OK;
}

// ------------------------------------------------------------
//  Main processing loop (equivalent to CLI main())
// ------------------------------------------------------------
ConverterError converter_process_files(
    Converter* c,
    const char** files,
    int file_count
) {
    if (!c || !files || file_count <= 0)
        return ERR_INVALID_OPTIONS;

    c->stop_flag = 0;

    char effective_output_dir[1024];
    ConverterError preflight_err = ensure_output_dir_writable(
        c,
        &c->opts,
        effective_output_dir,
        sizeof(effective_output_dir)
    );
    if (preflight_err != ERR_OK)
        return preflight_err;

    for (int i = 0; i < file_count; i++) {

        const char* input = files[i];

        // notify begin
        if (c->cb.on_file_begin)
            c->cb.on_file_begin(input, i + 1, file_count);

        // stop requested?
        if (c->stop_flag)
            return ERR_SKIP_FILE;

        // check input file
        ConverterError err = check_file(c, input);
        if (err != ERR_OK) {
            if (c->cb.on_file_end)
                c->cb.on_file_end(input, err);
            continue;
        }

        // generate output name
        char output[1024];
        ConvertOptions file_opts = c->opts;
        strncpy(file_opts.output_dir, effective_output_dir, sizeof(file_opts.output_dir) - 1);
        file_opts.output_dir[sizeof(file_opts.output_dir) - 1] = 0;
        make_output_name(input, &file_opts, output, sizeof(output));

        // check output existence
        err = check_output_exists(c, output);
        if (err == ERR_OUTPUT_EXISTS) {
            if (c->cb.on_file_end)
                c->cb.on_file_end(input, ERR_SKIP_FILE);
            continue;
        }

        // stop requested?
        if (c->stop_flag)
            return ERR_SKIP_FILE;

        // ----------------------------------------------------
        //  Peak 2-pass
        // ----------------------------------------------------
        if (strcmp(c->opts.audio_norm, "peak_norm_2pass") == 0) {
            double gain = 0.0;
            err = peak_two_pass(c, input, &gain);
            if (err != ERR_OK) {
                if (c->cb.on_file_end)
                    c->cb.on_file_end(input, err);
                continue;
            }
            c->opts.gain = gain;
        }

        // ----------------------------------------------------
        //  Loudnorm 2-pass
        // ----------------------------------------------------
        if (strcmp(c->opts.audio_norm, "loudness_norm_2pass") == 0) {

            double I = 0, TP = 0, LRA = 0, thresh = 0, offset = 0;

            // ----------------------------------------------------
            //  Loudnorm 2-pass genre selection (fixed logic)
            // ----------------------------------------------------
            double I_target  = -11;
            double TP_target = -1.5;
            double LRA_target = 7;

            if (c->opts.genre != 0) {
                switch (c->opts.genre) {
                case 1: I_target = -11; TP_target = -1.5; LRA_target = 6;  break; // EDM
                case 2: I_target = -11; TP_target = -1.0; LRA_target = 7;  break; // Rock
                case 3: I_target = -12; TP_target = -1.0; LRA_target = 6;  break; // Hip-Hop
                case 4: I_target = -16; TP_target = -2.0; LRA_target = 12; break; // Classical
                case 5: I_target = -16; TP_target = -1.5; LRA_target = 7;  break; // Podcast
                default:
                    I_target  = -11;
                    TP_target = -1.5;
                    LRA_target = 7;
                    break;
                }
            }

            err = loudnorm_two_pass(
                c,
                input,
                I_target, TP_target, LRA_target,
                &I, &TP, &LRA, &thresh, &offset
            );

            if (err != ERR_OK) {
                if (c->cb.on_file_end)
                    c->cb.on_file_end(input, err);
                continue;
            }

            // store results
            c->opts.I_target = I_target;
            c->opts.TP_target = TP_target;
            c->opts.LRA_target = LRA_target;
            c->opts.measured_I = I;
            c->opts.measured_TP = TP;
            c->opts.measured_LRA = LRA;
            c->opts.measured_thresh = thresh;
            c->opts.measured_offset = offset;
        }

        // stop requested?
        if (c->stop_flag)
            return ERR_SKIP_FILE;

        // ----------------------------------------------------
        //  Build ffmpeg command
        // ----------------------------------------------------
        char cmd[16384];
        c->opts.use_aac_for_h265 = codec_uses_aac_audio(c->opts.codec) ? 1 : 0;
        /* Platform-specific bitrate calculation for VideoToolbox is handled
         * inside platform_get_video_codec_flags() in converter_macos.c. */
        build_ffmpeg_cmd(c, input, output, cmd, sizeof(cmd));

        // ----------------------------------------------------
        //  Encoding
        // ----------------------------------------------------
        double duration = get_duration(input);
        err = run_ffmpeg_encode_with_progress(c, cmd, duration);
        if (err != ERR_OK) {
            if (c->cb.on_file_end)
                c->cb.on_file_end(input, err);
            continue;
        }

        // notify end
        if (c->cb.on_file_end)
            c->cb.on_file_end(input, ERR_OK);
    }

    // queue complete
    if (c->cb.on_complete)
        c->cb.on_complete();

    return ERR_OK;
}
