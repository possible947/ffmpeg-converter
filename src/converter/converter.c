#include "converter.h"
#include "subtitle_preservation.h"
#include "audio_orchestrator.h"
#include "converter_platform.h"
#include "input_video_info.h"
#include "converter_common.h"
#include "mux_policy.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
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
            strcmp(codec, "hevc_vaapi") == 0 ||
            strcmp(codec, "av1_vaapi") == 0 ||
            strcmp(codec, "hevc_vaapi_10bit") == 0 ||
            strcmp(codec, "av1_vaapi_10bit") == 0);
}

static int codec_is_qsv(const char* codec) {
    return codec &&
           (strcmp(codec, "h264_qsv") == 0 ||
            strcmp(codec, "hevc_qsv") == 0 ||
            strcmp(codec, "av1_qsv") == 0 ||
            strcmp(codec, "hevc_qsv_10bit") == 0 ||
            strcmp(codec, "av1_qsv_10bit") == 0);
}

static int codec_is_nvenc(const char* codec) {
    return codec &&
           (strcmp(codec, "h264_nvenc") == 0 ||
            strcmp(codec, "hevc_nvenc") == 0 ||
            strcmp(codec, "av1_nvenc") == 0 ||
            strcmp(codec, "hevc_nvenc_10bit") == 0 ||
            strcmp(codec, "av1_nvenc_10bit") == 0);
}

static int codec_is_amf(const char* codec) {
    return codec &&
           (strcmp(codec, "h264_amf") == 0 ||
            strcmp(codec, "hevc_amf") == 0 ||
            strcmp(codec, "av1_amf") == 0);
}

static int codec_is_10bit(const char* codec) {
    return codec && strstr(codec, "_10bit") != NULL;
}

static int codec_is_vulkan(const char* codec) {
    return codec &&
           (strcmp(codec, "prores_ks_vulkan") == 0 ||
            strcmp(codec, "h264_vulkan") == 0 ||
            strcmp(codec, "hevc_vulkan") == 0 ||
            strcmp(codec, "av1_vulkan") == 0);
}

static int codec_uses_mov_container(const char* codec) {
    return codec &&
           (strcmp(codec, "prores") == 0 ||
            strcmp(codec, "prores_ks") == 0 ||
            strcmp(codec, "prores_videotoolbox") == 0 ||
            strcmp(codec, "prores_ks_vulkan") == 0);
}

typedef enum {
    MUX_POLICY_RESOLVE_INPUT_PROBE_FAILED = -1,
    MUX_POLICY_RESOLVE_FAILED = 0,
    MUX_POLICY_RESOLVE_OK = 1
} MuxPolicyResolveResult;

static MuxPolicyResolveResult resolve_mux_output_policy(
    const char *input,
    const ConvertOptions *opts,
    MuxPolicy *policy)
{
    MuxBuildProfile profile;
    MuxMode mode;
    MuxVideoCodec source_codec = MUX_VIDEO_UNKNOWN;
    const char *audio_mode;
    char error[128];

    if (!input || !opts || !policy || !mux_build_profile_default(&profile))
        return 0;

    if (strcmp(opts->codec, "copy") == 0) {
        InputVideoInfo info;
        if (!input_video_info_probe(input, &info))
            return MUX_POLICY_RESOLVE_INPUT_PROBE_FAILED;
        source_codec = mux_video_codec_parse(info.codec_name);
        if (opts->preset[0] == '\0' || strcmp(opts->preset, "default") == 0)
            mode = MUX_MODE_COPY;
        else
            mode = mux_mode_parse(opts->preset);
        if (mode == MUX_MODE_INVALID)
            return MUX_POLICY_RESOLVE_FAILED;
    } else if (strcmp(opts->codec, "mux") == 0) {
        mode = mux_mode_parse(opts->preset[0] ? opts->preset : "mkv");
    } else {
        return MUX_POLICY_RESOLVE_FAILED;
    }

    audio_mode = opts->audio_output_mode[0] ? opts->audio_output_mode : NULL;
    return mux_policy_resolve(&profile, mode, audio_mode, source_codec,
                              MUX_VIDEO_UNKNOWN, policy, error, sizeof(error))
               ? MUX_POLICY_RESOLVE_OK
               : MUX_POLICY_RESOLVE_FAILED;
}

static int input_has_attachment_stream(const char *input)
{
    const char *ffprobe = platform_get_ffprobe_bin();
    char *quoted_tool;
    char *quoted_input;
    char command[8192];
    char line[128];
    FILE *pipe;
    int found = 0;
    int status;

    if (!input || !ffprobe || !ffprobe[0])
        return -1;
    quoted_tool = platform_escape_path_for_command(ffprobe);
    quoted_input = platform_escape_path_for_command(input);
    if (!quoted_tool || !quoted_input) {
        free(quoted_tool);
        free(quoted_input);
        return -1;
    }
    status = snprintf(command, sizeof(command),
                      "%s -v error -select_streams t -show_entries stream=index "
                      "-of csv=p=0 %s 2>%s",
                      quoted_tool, quoted_input, platform_get_null_device());
    free(quoted_tool);
    free(quoted_input);
    if (status < 0 || (size_t)status >= sizeof(command))
        return -1;

    pipe = platform_popen(command, "r");
    if (!pipe)
        return -1;
    while (fgets(line, sizeof(line), pipe)) {
        if (line[0] != '\0' && strspn(line, "\r\n") != strlen(line)) {
            found = 1;
            break;
        }
    }
    status = platform_pclose(pipe);
    return status == 0 ? found : -1;
}

/* Convert preset string ("lt", "standard", "hq", "4444", "default") to ffmpeg profile number */
static int preset_to_profile_num(const char* preset) {
    if (!preset || preset[0] == '\0') {
        return 2;  /* standard */
    }
    if (strcmp(preset, "lt") == 0) {
        return 1;
    }
    if (strcmp(preset, "hq") == 0) {
        return 3;
    }
    if (strcmp(preset, "4444") == 0) {
        return 4;
    }
    return 2;  /* standard or default */
}

static int cmd_cat(char* buf, size_t buf_sz, size_t* pos, const char* s);

static int append_color_metadata(char *cmd, size_t cmd_size, size_t *pos,
                                 const InputVideoInfo *info)
{
    char option[160];

    if (!info)
        return 0;
    if (info->color_range[0] != '\0' && strcmp(info->color_range, "N/A") != 0) {
        snprintf(option, sizeof(option), "-color_range %s ", info->color_range);
        if (cmd_cat(cmd, cmd_size, pos, option) < 0) return -1;
    }
    if (info->color_primaries[0] != '\0' && strcmp(info->color_primaries, "N/A") != 0) {
        snprintf(option, sizeof(option), "-color_primaries %s ", info->color_primaries);
        if (cmd_cat(cmd, cmd_size, pos, option) < 0) return -1;
    }
    if (info->color_transfer[0] != '\0' && strcmp(info->color_transfer, "N/A") != 0) {
        snprintf(option, sizeof(option), "-color_trc %s ", info->color_transfer);
        if (cmd_cat(cmd, cmd_size, pos, option) < 0) return -1;
    }
    if (info->color_space[0] != '\0' && strcmp(info->color_space, "N/A") != 0) {
        snprintf(option, sizeof(option), "-colorspace %s ", info->color_space);
        if (cmd_cat(cmd, cmd_size, pos, option) < 0) return -1;
    }
    return 0;
}

static int codec_uses_aac_audio(const char* codec) {
    return codec &&
           (strcmp(codec, "hevc_videotoolbox") == 0);
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

    if (!audio_output_mode_is_valid(c->opts.audio_output_mode))
        return ERR_INVALID_OPTIONS;

    /* COPY mode does no audio encoding or filtering; skip validation. */
    AudioOutputMode audio_mode = audio_output_mode_parse(c->opts.audio_output_mode);
    if (audio_mode != AUDIO_MODE_COPY) {
        /* Validate required audio filters */
        if (!platform_validate_audio_filters()) {
            if (c->cb.on_error)
                c->cb.on_error("required FFmpeg audio filters not available",
                               ERR_AUDIO_FILTER_VALIDATION_FAILED);
            return ERR_AUDIO_FILTER_VALIDATION_FAILED;
        }
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

int converter_is_stopped(Converter* c) {
    if (!c) return 1;
    return c->stop_flag;
}

const ConverterCallbacks* converter_get_callbacks(Converter* c) {
    if (!c) return NULL;
    return &c->cb;
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

    if (!platform_stat_is_directory(out_dir)) {
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
    const char *ffprobe_bin = get_ffprobe_bin();
    char* esc_ffprobe = platform_escape_path_for_command(ffprobe_bin);
    char* esc_input   = platform_escape_path_for_command(input);
    if (!esc_ffprobe || !esc_input) {
        free(esc_ffprobe);
        free(esc_input);
        return 0.0;
    }

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "%s -v error -show_entries format=duration "
             "-of default=noprint_wrappers=1:nokey=1 %s 2>%s",
             esc_ffprobe, esc_input, platform_get_null_device());

    free(esc_ffprobe);
    free(esc_input);

    FILE *fp = platform_popen(cmd, "r");
    if (!fp) return 0.0;

    char buf[256];
    int got_output = (fgets(buf, sizeof(buf), fp) != NULL);
    int status = platform_pclose(fp);
    if (!got_output || status != 0) {
        return 0.0;
    }

    return atof(buf);
}

// ------------------------------------------------------------
//  File checks
// ------------------------------------------------------------
static ConverterError check_file(Converter* c, const char *file) {
    if (!platform_stat_is_regular_file(file)) {
        /* Distinguish between a missing file and a non-regular file (directory,
         * symlink, etc.).  stat() itself is available on MSVC via <sys/stat.h>. */
        struct stat st;
        if (stat(file, &st) != 0) {
            if (c->cb.on_error)
                c->cb.on_error("input file not found", ERR_INPUT_NOT_FOUND);
            return ERR_INPUT_NOT_FOUND;
        }
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
    snprintf(base, sizeof(base), "%s", name);

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
    if (strcmp(opts->codec, "copy") == 0 || strcmp(opts->codec, "mux") == 0) {
        MuxPolicy policy;
        ext = resolve_mux_output_policy(input, opts, &policy) ==
                      MUX_POLICY_RESOLVE_OK
                  ? mux_container_extension(policy.container) : NULL;
        if (!ext)
            ext = "mkv";
    }
    else if (strcmp(opts->codec, "hevc_videotoolbox") == 0 ||
             strcmp(opts->codec, "h264_videotoolbox") == 0)
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
//  Build ffmpeg command (strictly same logic as CLI)
// ------------------------------------------------------------

/* Append string s to buf[0..buf_sz-1], tracking offset *pos.
 * Returns 0 on success, -1 if the buffer would overflow. */
static int cmd_cat(char* buf, size_t buf_sz, size_t* pos, const char* s) {
    size_t n = strlen(s);
    if (*pos + n + 1 > buf_sz) return -1;
    memcpy(buf + *pos, s, n + 1);
    *pos += n;
    return 0;
}

static void build_ffmpeg_cmd(
    Converter* c,
    const char* input,
    const char* output,
    char* cmd_out,
    size_t cmd_out_sz
) {
    const ConvertOptions* opts = &c->opts;
    const char* ffmpeg_bin = get_ffmpeg_bin();
    MuxPolicy copy_policy;
    int have_copy_policy = 0;

    /* Resolve audio mode and encoder capabilities via the audio module */
    AudioOutputMode audio_mode = audio_output_mode_parse(opts->audio_output_mode);
    if (audio_mode == AUDIO_MODE_INVALID)
        audio_mode = AUDIO_MODE_PCM; /* default fallback */
    if (strcmp(opts->codec, "copy") == 0) {
        if (resolve_mux_output_policy(input, opts, &copy_policy) !=
            MUX_POLICY_RESOLVE_OK) {
            if (c->cb.on_error)
                c->cb.on_error("Copy mode policy resolution failed", ERR_INVALID_OPTIONS);
            cmd_out[0] = '\0';
            return;
        }
        have_copy_policy = 1;
    }

    AudioEncoderCaps caps;
    audio_probe_encoders(&caps);

    AudioConfiguration audio_cfg;
    int cfg_ok = audio_resolve_configuration(audio_mode, &caps, &audio_cfg) == 0;

    /* Track selection policy from configuration */
    AudioTrackPolicy track_policy = AUDIO_TRACK_POLICY_FIRST;
    if (cfg_ok)
        track_policy = audio_cfg.track_policy;
    if (have_copy_policy)
        track_policy = copy_policy.audio_track_policy == MUX_AUDIO_TRACK_ALL
                           ? AUDIO_TRACK_POLICY_ALL : AUDIO_TRACK_POLICY_FIRST;

    /* For use_aac_for_h265 path: resolve separately */
    int is_dual_audio_output = cfg_ok && audio_cfg.is_dual_output;
    int is_fdk_single_audio_output =
        audio_mode == AUDIO_MODE_FDK_AAC_320;
    int is_copy_audio_output =
        audio_mode == AUDIO_MODE_COPY;
    int has_aac_at = caps.has_aac_at;
    int has_libfdk_aac = caps.has_libfdk_aac;
    int has_aac_native = caps.has_aac_native;

    /* Build the filter expression via the audio module */
    char audio_filter[1024];
    AudioNormalizationMode norm_mode =
        audio_normalization_mode_parse(opts->audio_norm);
    if (norm_mode == AUDIO_NORM_INVALID)
        norm_mode = AUDIO_NORM_NONE;

    PeakAnalysisResult peak_res = {0};
    LoudnormAnalysisResult loudnorm_res = {0};
    if (norm_mode == AUDIO_NORM_PEAK_2PASS) {
        peak_res.gain = opts->gain;
        peak_res.valid = 1;
    }
    if (norm_mode == AUDIO_NORM_LOUDNESS_2PASS) {
        loudnorm_res.I_target = opts->I_target;
        loudnorm_res.TP_target = opts->TP_target;
        loudnorm_res.LRA_target = opts->LRA_target;
        loudnorm_res.measured_I = opts->measured_I;
        loudnorm_res.measured_TP = opts->measured_TP;
        loudnorm_res.measured_LRA = opts->measured_LRA;
        loudnorm_res.measured_thresh = opts->measured_thresh;
        loudnorm_res.target_offset = opts->measured_offset;
        loudnorm_res.valid = 1;
    }
    audio_build_filter_expression(norm_mode, &peak_res, &loudnorm_res,
                                  audio_filter, sizeof(audio_filter));

    InputVideoInfo input_info;
    int have_input_info = input_video_info_probe(input, &input_info);
    int selected_10bit = codec_is_10bit(opts->codec);
    int output_hw = codec_is_vaapi(opts->codec) || codec_is_qsv(opts->codec) ||
                    codec_is_nvenc(opts->codec) || codec_is_amf(opts->codec) ||
                    codec_is_vulkan(opts->codec);

    if (have_input_info && output_hw && input_info.bit_depth > 0) {
        if (selected_10bit && input_info.bit_depth < 10 && c->cb.on_message)
            c->cb.on_message("Input video is 8-bit; converting to 10-bit for the selected encoder.");
        else if (!selected_10bit && input_info.bit_depth >= 10 && c->cb.on_message)
            c->cb.on_message("Input video is 10-bit; converting to 8-bit for the selected encoder.");
    }
    if (have_input_info && output_hw && input_info.bit_depth == 0 && c->cb.on_message)
        c->cb.on_message("Input video bit depth is unknown; using the selected encoder format.");
    if (strcmp(opts->codec, "copy") == 0) {
        int has_attachments;
        has_attachments = input_has_attachment_stream(input);
        if (copy_policy.attachment_action == MUX_STREAM_DROP &&
            has_attachments < 0) {
            if (c->cb.on_error)
                c->cb.on_error("Cannot verify attachment compatibility for Copy mode",
                               ERR_INVALID_OPTIONS);
            cmd_out[0] = '\0';
            return;
        }
        if (copy_policy.attachment_action == MUX_STREAM_DROP && has_attachments &&
            c->cb.on_message)
            c->cb.on_message(
                "Warning: input attachments are dropped by the selected output container.");
    }

    /* Shell-safe escaped versions of all user-provided paths */
    char* esc_ffmpeg = platform_escape_path_for_command(ffmpeg_bin);
    char* esc_input  = platform_escape_path_for_command(input);
    char* esc_output = platform_escape_path_for_command(output);
    if (!esc_ffmpeg || !esc_input || !esc_output) {
        free(esc_ffmpeg);
        free(esc_input);
        free(esc_output);
        if (c->cb.on_message)
            c->cb.on_message("ffmpeg command build failed: out of memory");
        cmd_out[0] = '\0';
        return;
    }

    /* Use a large internal buffer to avoid truncation */
    char cmd[65536];
    size_t pos = 0;
    cmd[0] = '\0';

    /* ffmpeg binary */
    if (cmd_cat(cmd, sizeof(cmd), &pos, esc_ffmpeg) < 0) goto overflow;
    if (cmd_cat(cmd, sizeof(cmd), &pos, " ") < 0) goto overflow;

    if (opts->overwrite) {
        if (cmd_cat(cmd, sizeof(cmd), &pos, "-y ") < 0) goto overflow;
    } else {
        if (cmd_cat(cmd, sizeof(cmd), &pos, "-n ") < 0) goto overflow;
    }

    /* Pre-input hardware device initialisation (Vulkan, future HW APIs).
     * Must appear before -i so ffmpeg can locate the device context. */
    {
        const char* pre_hw = platform_get_preinput_hw_flags(opts->codec, opts);
        if (pre_hw && pre_hw[0] != '\0') {
            if (cmd_cat(cmd, sizeof(cmd), &pos, pre_hw) < 0) goto overflow;
            if (cmd_cat(cmd, sizeof(cmd), &pos, " ") < 0) goto overflow;
        }
    }

    /* VAAPI requires a device node before the input */
    if (codec_is_vaapi(opts->codec) && opts->hw_device[0] != '\0') {
        char* esc_hw = platform_escape_path_for_command(opts->hw_device);
        if (!esc_hw) {
            free(esc_ffmpeg); free(esc_input); free(esc_output);
            cmd_out[0] = '\0';
            return;
        }
        if (cmd_cat(cmd, sizeof(cmd), &pos, "-vaapi_device ") < 0 ||
            cmd_cat(cmd, sizeof(cmd), &pos, esc_hw) < 0 ||
            cmd_cat(cmd, sizeof(cmd), &pos, " ") < 0) {
            free(esc_hw);
            goto overflow;
        }
        free(esc_hw);
    }

    /* Select input decoder.
     * The native `av1` decoder in ffmpeg builds with --enable-nvdec tries
     * NVDEC pixel formats internally even with -hwaccel none.  On systems
     * where the NVIDIA GPU does not support AV1 hardware decode, this
     * causes a fatal "Failed to get pixel format" error.  We avoid this
     * by using av1_qsv (Intel QSV/D3D11VA) when available, which uses the
     * Intel GPU instead.  All other codecs use -hwaccel none (software). */
    {
        InputVideoInfo input_info;
        int input_is_av1 = input_video_info_probe(input, &input_info) &&
                   strcmp(input_info.codec_name, "av1") == 0;
        int output_is_hw_encode = codec_is_vaapi(opts->codec) ||
                                   codec_is_vulkan(opts->codec);
        if (input_is_av1 && output_is_hw_encode &&
            (c->platform_caps & PLAT_CAP_LIBDAV1D_DEC)) {
            /* Keep hardware decode and hardware encode on separate contexts.
             * QSV decode produces frames that cannot be uploaded reliably to
             * the selected VAAPI/Vulkan device on mixed-GPU systems. */
            if (cmd_cat(cmd, sizeof(cmd), &pos,
                        "-hwaccel none -c:v libdav1d ") < 0) goto overflow;
        } else if (input_is_av1 && !output_is_hw_encode &&
                   (c->platform_caps & PLAT_CAP_AV1_QSV_DEC)) {
            /* Intel QSV AV1 decoder via D3D11VA: bypasses broken native av1
             * decoder on systems with NVDEC that doesn't support AV1.  The
             * hwaccel_output_format=nv12 ensures CPU-readable frames for
             * downstream software encoders (prores_ks, prores, etc.). */
            if (cmd_cat(cmd, sizeof(cmd), &pos,
                        "-hwaccel qsv -hwaccel_output_format nv12 "
                        "-c:v av1_qsv ") < 0) goto overflow;
        } else if (input_is_av1 && (c->platform_caps & PLAT_CAP_LIBDAV1D_DEC)) {
            /* libdav1d: pure software AV1 decoder, no hardware dependency.
             * Bypasses the native av1 decoder which probes NVDEC/VAAPI
             * pixel formats and crashes when the GPU lacks AV1 decode
             * support.  Works on all platforms including Linux without QSV. */
            if (cmd_cat(cmd, sizeof(cmd), &pos, "-hwaccel none -c:v libdav1d ") < 0) goto overflow;
        } else {
            /* Software decode for all other inputs (VP9, H264, HEVC, etc.).
             * Also used as AV1 fallback when neither QSV nor libdav1d
             * is available (may fail on NVDEC systems for AV1). */
            if (cmd_cat(cmd, sizeof(cmd), &pos, "-hwaccel none ") < 0) goto overflow;
        }
    }

    if (cmd_cat(cmd, sizeof(cmd), &pos, "-i ") < 0 ||
        cmd_cat(cmd, sizeof(cmd), &pos, esc_input) < 0 ||
        cmd_cat(cmd, sizeof(cmd), &pos, " ") < 0) goto overflow;

    // map
    if (cmd_cat(cmd, sizeof(cmd), &pos, "-map 0:v:0 ") < 0) goto overflow;
    if (is_dual_audio_output) {
        if (cmd_cat(cmd, sizeof(cmd), &pos, "-filter_complex \"[0:a:0]") < 0 ||
            cmd_cat(cmd, sizeof(cmd), &pos, audio_filter) < 0 ||
            cmd_cat(cmd, sizeof(cmd), &pos, ",asplit=2[aout0][aout1]\" ") < 0 ||
            cmd_cat(cmd, sizeof(cmd), &pos, "-map [aout0] -map [aout1] ") < 0) goto overflow;
    } else if (track_policy == AUDIO_TRACK_POLICY_ALL) {
        if (cmd_cat(cmd, sizeof(cmd), &pos, "-map 0:a ") < 0) goto overflow;
    } else {
        if (cmd_cat(cmd, sizeof(cmd), &pos, "-map 0:a:0 ") < 0) goto overflow;
    }
    if (strcmp(opts->codec, "copy") == 0) {
        char subtitle_args[8192];
        if (!subtitle_preservation_build(input, copy_policy.container,
                                         subtitle_args, sizeof(subtitle_args), &c->cb)) {
            if (c->cb.on_message)
                c->cb.on_message("Cannot resolve subtitle preservation policy.");
            free(esc_ffmpeg);
            free(esc_input);
            free(esc_output);
            cmd_out[0] = '\0';
            return;
        }
        if (cmd_cat(cmd, sizeof(cmd), &pos, subtitle_args) < 0)
            goto overflow;
    }
    if ((!have_copy_policy ||
         copy_policy.metadata_action == MUX_STREAM_PRESERVE) &&
        cmd_cat(cmd, sizeof(cmd), &pos, "-map_metadata 0 ") < 0)
        goto overflow;
    if (have_copy_policy && copy_policy.chapter_action == MUX_STREAM_PRESERVE &&
        cmd_cat(cmd, sizeof(cmd), &pos, "-map_chapters 0 ") < 0) goto overflow;
    if (have_copy_policy && copy_policy.attachment_action == MUX_STREAM_PRESERVE &&
        cmd_cat(cmd, sizeof(cmd), &pos, "-map 0:t? ") < 0) goto overflow;
    if (append_color_metadata(cmd, sizeof(cmd), &pos,
                              have_input_info ? &input_info : NULL) < 0)
        goto overflow;

    // video codec
    // Try platform-specific codec flags first (VAAPI, VideoToolbox, NVENC, etc.)
    const char* platform_vcodec = platform_get_video_codec_flags(opts->codec, input, opts);
        if (platform_vcodec != NULL) {
        if (cmd_cat(cmd, sizeof(cmd), &pos, platform_vcodec) < 0) goto overflow;
            if (selected_10bit && (strstr(opts->codec, "hevc_") != NULL)) {
                if (cmd_cat(cmd, sizeof(cmd), &pos, "-profile:v main10 ") < 0) goto overflow;
            }
    }
    else if (output_hw) {
        if (c->cb.on_message)
            c->cb.on_message("ffmpeg command build failed: preset ffmpeg_args not found");
        goto command_invalid;
    }
    else if (strcmp(opts->codec, "prores") == 0 ||
             strcmp(opts->codec, "prores_ks") == 0)
    {
        int profile_value = preset_to_profile_num(opts->preset);

        if (strcmp(opts->codec, "prores_ks") == 0) {
            const char* profile_name = "standard";
            if (profile_value == 1) profile_name = "lt";
            else if (profile_value == 3) profile_name = "hq";
            else if (profile_value == 4) profile_name = "4444";

            char tmp[160];
            snprintf(tmp, sizeof(tmp),
                     "-c:v prores_ks -profile:v %s ",
                     profile_name);
            if (cmd_cat(cmd, sizeof(cmd), &pos, tmp) < 0) goto overflow;
        } else {
            char tmp[128];
            snprintf(tmp, sizeof(tmp),
                     "-c:v prores -profile:v %d ",
                     profile_value);
            if (cmd_cat(cmd, sizeof(cmd), &pos, tmp) < 0) goto overflow;
        }
    }
    else {
        if (cmd_cat(cmd, sizeof(cmd), &pos, "-c:v copy ") < 0) goto overflow;
    }

    // deblock (not applicable for hardware encoders or hw-upload codecs)
    if (platform_vcodec == NULL && !codec_is_vaapi(opts->codec) && !codec_is_vulkan(opts->codec)) {
        if (opts->deblock == 2) {
            if (cmd_cat(cmd, sizeof(cmd), &pos,
                    "-vf \"deblock=filter=weak:block=4:planes=1\" ") < 0) goto overflow;
        }
        else if (opts->deblock == 3) {
            if (cmd_cat(cmd, sizeof(cmd), &pos,
                    "-vf \"deblock=filter=strong:block=4:"
                    "alpha=0.12:beta=0.07:gamma=0.06:delta=0.05:planes=1\" ") < 0) goto overflow;
        }
    }
    else if (codec_is_vaapi(opts->codec) || codec_is_vulkan(opts->codec) ||
             codec_is_qsv(opts->codec) || codec_is_nvenc(opts->codec)) {
        /* Pixel format conversion and GPU upload for hw-accelerated codecs.
         * The filter string is provided by the platform; defaults to VAAPI. */
        const char* hw_vf = platform_get_hw_vfilter(opts->codec, opts);
        const char *format = selected_10bit ? "p010le" : "nv12";
        if ((codec_is_qsv(opts->codec) || codec_is_nvenc(opts->codec)) && !selected_10bit)
            goto skip_hw_filter;
        if (cmd_cat(cmd, sizeof(cmd), &pos, "-vf \"format=") < 0 ||
            cmd_cat(cmd, sizeof(cmd), &pos,
                selected_10bit ? ((codec_is_qsv(opts->codec) || codec_is_nvenc(opts->codec)) ? "p010le" : "p010le,hwupload") :
                (codec_is_qsv(opts->codec) || codec_is_nvenc(opts->codec)) ? format :
                ((hw_vf && hw_vf[0] != '\0') ? hw_vf : "nv12,hwupload")) < 0 ||
            cmd_cat(cmd, sizeof(cmd), &pos, "\" ") < 0) goto overflow;
    }
    skip_hw_filter:

    // audio codec
    if (is_dual_audio_output) {
        if (has_aac_at) {
            if (cmd_cat(cmd, sizeof(cmd), &pos, "-c:a:0 aac_at -b:a:0 320k -ar:a:0 48000 ") < 0) goto overflow;
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: aac_at (CBR 320k)");
        } else if (has_libfdk_aac) {
            if (cmd_cat(cmd, sizeof(cmd), &pos, "-c:a:0 libfdk_aac -b:a:0 320k -ar:a:0 48000 ") < 0) goto overflow;
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: libfdk_aac (CBR 320k)");
        } else {
            if (c->cb.on_error)
                c->cb.on_error("fdk_aac mode requires libfdk_aac but encoder is not available",
                               ERR_INVALID_OPTIONS);
            free(esc_ffmpeg); free(esc_input); free(esc_output);
            cmd_out[0] = '\0';
            return;

        command_invalid:
            free(esc_ffmpeg);
            free(esc_input);
            free(esc_output);
            cmd_out[0] = '\0';
            return;
        }
        if (cmd_cat(cmd, sizeof(cmd), &pos, "-c:a:1 ac3 -b:a:1 640k -ar:a:1 48000 ") < 0) goto overflow;
    } else if (is_fdk_single_audio_output) {
        if (has_aac_at) {
            if (cmd_cat(cmd, sizeof(cmd), &pos, "-c:a aac_at -b:a 320k -ar 48000 ") < 0) goto overflow;
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: aac_at (CBR 320k)");
        } else if (has_libfdk_aac) {
            if (cmd_cat(cmd, sizeof(cmd), &pos, "-c:a libfdk_aac -b:a 320k -ar 48000 ") < 0) goto overflow;
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: libfdk_aac (CBR 320k)");
        } else {
            if (c->cb.on_error)
                c->cb.on_error("fdk_aac mode requires libfdk_aac but encoder is not available",
                               ERR_INVALID_OPTIONS);
            free(esc_ffmpeg); free(esc_input); free(esc_output);
            cmd_out[0] = '\0';
            return;
        }
    } else if (is_copy_audio_output) {
        if (cmd_cat(cmd, sizeof(cmd), &pos, "-c:a copy ") < 0) goto overflow;
    } else if (audio_mode == AUDIO_MODE_PCM) {
        if (cmd_cat(cmd, sizeof(cmd), &pos, "-c:a pcm_s16le -ar 48000 ") < 0) goto overflow;
    } else if (c->opts.use_aac_for_h265) {
        if (has_aac_at) {
            if (cmd_cat(cmd, sizeof(cmd), &pos, "-c:a aac_at -b:a 320k -ar 48000 ") < 0) goto overflow;
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: aac_at (CBR 320k)");
        } else if (has_libfdk_aac) {
            if (cmd_cat(cmd, sizeof(cmd), &pos, "-c:a libfdk_aac -b:a 320k -ar 48000 ") < 0) goto overflow;
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: libfdk_aac (CBR 320k)");
        } else {
            if (cmd_cat(cmd, sizeof(cmd), &pos, "-c:a aac -b:a 320k -ar 48000 ") < 0) goto overflow;
            if (c->cb.on_message) c->cb.on_message("AAC encoder selected: native aac (CBR 320k)");
        }
    } else {
        if (cmd_cat(cmd, sizeof(cmd), &pos, "-c:a pcm_s16le -ar 48000 ") < 0) goto overflow;
    }

    if (!is_dual_audio_output && !is_copy_audio_output) {
        if (cmd_cat(cmd, sizeof(cmd), &pos, "-af \"") < 0 ||
            cmd_cat(cmd, sizeof(cmd), &pos, audio_filter) < 0 ||
            cmd_cat(cmd, sizeof(cmd), &pos, "\" ") < 0) goto overflow;
    }

    // ffmpeg progress options must be placed before output
    if (cmd_cat(cmd, sizeof(cmd), &pos, "-progress pipe:1 -nostats -nostdin ") < 0) goto overflow;

    // output
    if (cmd_cat(cmd, sizeof(cmd), &pos, esc_output) < 0) goto overflow;

    free(esc_ffmpeg);
    free(esc_input);
    free(esc_output);

    // copy to output buffer
    strncpy(cmd_out, cmd, cmd_out_sz);
    cmd_out[cmd_out_sz - 1] = '\0';

    if (c->cb.on_message) {
        c->cb.on_message("ffmpeg command built");
    }
    return;

overflow:
    free(esc_ffmpeg);
    free(esc_input);
    free(esc_output);
    if (c->cb.on_message)
        c->cb.on_message("ffmpeg command build failed: command too long");
    cmd_out[0] = '\0';
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

    FILE* fp = platform_popen(cmd, "r");
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
            platform_pclose(fp);
            return ERR_SKIP_FILE;
        }
    }

    int status = platform_pclose(fp);
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

    if (strcmp(c->opts.codec, "copy") == 0) {
        int i;
        for (i = 0; i < file_count; ++i) {
            MuxPolicy policy;
            if (!files[i] ||
                !platform_stat_is_regular_file(files[i]) ||
                !platform_is_file_readable(files[i]))
                continue;
            MuxPolicyResolveResult result =
                resolve_mux_output_policy(files[i], &c->opts, &policy);
            if (result == MUX_POLICY_RESOLVE_INPUT_PROBE_FAILED)
                continue;
            if (result != MUX_POLICY_RESOLVE_OK) {
                if (c->cb.on_error)
                    c->cb.on_error("Copy mode policy validation failed",
                                   ERR_INVALID_OPTIONS);
                return ERR_INVALID_OPTIONS;
            }
        }
    }

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
        //  Peak 2-pass (skip for COPY mode — no audio encoding)
        // ----------------------------------------------------
        if (strcmp(c->opts.audio_norm, "peak_norm_2pass") == 0 &&
            strcmp(c->opts.audio_output_mode, "copy") != 0) {
            PeakAnalysisResult peak_result;
            err = audio_peak_analyze(input, &c->cb, &c->stop_flag, &peak_result);
            if (err != ERR_OK) {
                if (c->cb.on_file_end)
                    c->cb.on_file_end(input, err);
                continue;
            }
            c->opts.gain = peak_result.gain;
        }

        // ----------------------------------------------------
        //  Loudnorm 2-pass (skip for COPY mode — no audio encoding)
        // ----------------------------------------------------
        if (strcmp(c->opts.audio_norm, "loudness_norm_2pass") == 0 &&
            strcmp(c->opts.audio_output_mode, "copy") != 0) {
            AudioGenre genre = audio_genre_parse(c->opts.genre);
            LoudnormAnalysisResult ln_result;
            err = audio_loudnorm_analyze(input, genre, &c->cb,
                                         &c->stop_flag, &ln_result);
            if (err != ERR_OK) {
                if (c->cb.on_file_end)
                    c->cb.on_file_end(input, err);
                continue;
            }

            /* store results */
            c->opts.I_target = ln_result.I_target;
            c->opts.TP_target = ln_result.TP_target;
            c->opts.LRA_target = ln_result.LRA_target;
            c->opts.measured_I = ln_result.measured_I;
            c->opts.measured_TP = ln_result.measured_TP;
            c->opts.measured_LRA = ln_result.measured_LRA;
            c->opts.measured_thresh = ln_result.measured_thresh;
            c->opts.measured_offset = ln_result.target_offset;
        }

        // stop requested?
        if (c->stop_flag)
            return ERR_SKIP_FILE;

        // ----------------------------------------------------
        //  Build ffmpeg command
        // ----------------------------------------------------
        char cmd[65536];
        c->opts.use_aac_for_h265 = codec_uses_aac_audio(c->opts.codec) ? 1 : 0;
        /* Platform-specific bitrate calculation for VideoToolbox is handled
         * inside platform_get_video_codec_flags() in converter_macos.c. */
        build_ffmpeg_cmd(c, input, output, cmd, sizeof(cmd));

        if (cmd[0] == '\0') {
            if (c->cb.on_error)
                c->cb.on_error("ffmpeg command build failed", ERR_INVALID_OPTIONS);
            if (c->cb.on_file_end)
                c->cb.on_file_end(input, ERR_INVALID_OPTIONS);
            continue;
        }

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
