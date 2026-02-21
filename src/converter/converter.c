#include "converter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <jansson.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

struct Converter {
    ConvertOptions opts;
    ConverterCallbacks cb;
    int stop_flag;
};

// ------------------------------------------------------------
//  Create / Destroy
// ------------------------------------------------------------
Converter* converter_create(void) {
    Converter* c = calloc(1, sizeof(Converter));
    return c;
}

void converter_destroy(Converter* c) {
    if (!c) return;
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
        default: return "unknown error";
    }
}

// ------------------------------------------------------------
//  Helpers: time parsing
// ------------------------------------------------------------
static double parse_time_hms(const char *s) {
    int h = 0, m = 0;
    double sec = 0.0;
    if (sscanf(s, "%d:%d:%lf", &h, &m, &sec) == 3) {
        return h * 3600.0 + m * 60.0 + sec;
    }
    return 0.0;
}

static void format_eta(double eta, char *buf, size_t sz) {
    if (eta <= 0) {
        snprintf(buf, sz, "ETA --:--:--");
        return;
    }
    int t = (int)eta;
    int h = t / 3600;
    int m = (t % 3600) / 60;
    int s = t % 60;
    snprintf(buf, sz, "ETA %02d:%02d:%02d", h, m, s);
}

// ------------------------------------------------------------
//  ffprobe duration
// ------------------------------------------------------------
static double get_duration(const char *input) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "ffprobe -v error -show_entries format=duration "
             "-of default=noprint_wrappers=1:nokey=1 \"%s\" 2>/dev/null",
             input);

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

    if (access(file, R_OK) != 0) {
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
    
    // 1. basename
    const char* slash = strrchr(input, '/');
#ifdef _WIN32
    const char* backslash = strrchr(input, '\\');
    if (backslash && (!slash || backslash > slash))
        slash = backslash;
#endif
    const char* name = slash ? slash + 1 : input;

    // 2. base without extension
    char base[512];
    // Копируем имя файла с безопасным ограничением
    size_t name_len = strlen(name);
    size_t copy_len = (name_len < sizeof(base) - 1) ? name_len : sizeof(base) - 1;
    strncpy(base, name, copy_len);
    base[copy_len] = '\0';

    // Удаляем расширение
    char* dot = strrchr(base, '.');
    if (dot) *dot = '\0';

    // 3. Формируем новое имя файла с безопасной длиной
    char filename[1024];
    size_t base_len = strlen(base);
    
    // Максимальная длина для base чтобы вместить "_converted.ext"
    size_t max_safe_base_len = sizeof(filename) - 15; // 15 = "_converted.ext\0"
    
    if (base_len <= max_safe_base_len) {
        // Имя вписывается нормально
        if (strcmp(opts->codec, "copy") == 0 ||
            strcmp(opts->codec, "h265_mi50") == 0)
            snprintf(filename, sizeof(filename), "%s_converted.mkv", base);
        else
            snprintf(filename, sizeof(filename), "%s_converted.mov", base);
    } else {
        // Имя слишком длинное - усекаем
        char truncated[512];
        snprintf(truncated, sizeof(truncated), "%s", base);
        
        if (strcmp(opts->codec, "copy") == 0 ||
            strcmp(opts->codec, "h265_mi50") == 0)
            snprintf(filename, sizeof(filename), "%s_converted.mkv", truncated);
        else
            snprintf(filename, sizeof(filename), "%s_converted.mov", truncated);
    }

    // 4. Если output_dir не указан
    if (opts->output_dir[0] == '\0') {
        strncpy(out, filename, out_sz - 1);
        out[out_sz - 1] = '\0';
        return;
    }

    // 5. С output_dir
    size_t dir_len = strlen(opts->output_dir);
    size_t filename_len = strlen(filename);
    size_t total_len = dir_len + 1 + filename_len; // +1 для '/'
    
    if (total_len < out_sz) {
        snprintf(out, out_sz, "%s/%s", opts->output_dir, filename);
    } else {
        // Слишком длинный путь - используем только имя файла
        if (filename_len < out_sz) {
            strncpy(out, filename, out_sz - 1);
            out[out_sz - 1] = '\0';
        } else {
            // Даже имя файла слишком длинное
            strncpy(out, "output", out_sz - 1);
            out[out_sz - 1] = '\0';
        }
    }
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
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -vn -i \"%s\" -af volumedetect -f null - 2>&1",
        input);

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
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -vn -i \"%s\" -af "
        "\"loudnorm=I=%.1f:TP=%.1f:LRA=%.1f:print_format=json\" "
        "-f null - 2>&1",
        input, I_target, TP_target, LRA_target);

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

    *I      = json_number_value(json_object_get(root, "input_i"));
    *TP     = json_number_value(json_object_get(root, "input_tp"));
    *LRA    = json_number_value(json_object_get(root, "input_lra"));
    *thresh = json_number_value(json_object_get(root, "input_thresh"));
    *offset = json_number_value(json_object_get(root, "target_offset"));

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

    char cmd[16384];
    cmd[0] = 0;

    // input
    if (strcmp(opts->codec, "h265_mi50") == 0) {
        const char* device = getenv("VAAPI_DEVICE");
        if (!device || device[0] == '\0')
            device = "/dev/dri/renderD128";
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -vaapi_device \"%s\" -i \"%s\" ",
                 device, input);
    } else {
        snprintf(cmd, sizeof(cmd), "ffmpeg -i \"%s\" ", input);
    }

    // map
    strcat(cmd, "-map 0:v:0 ");
    strcat(cmd, "-map 0:a:0 ");
    strcat(cmd, "-map_metadata 0 ");

    // video codec
    if (strcmp(opts->codec, "prores") == 0 ||
        strcmp(opts->codec, "prores_ks") == 0)
    {
        char tmp[128];
        snprintf(tmp, sizeof(tmp),
                 "-c:v %s -profile:v %d ",
                 opts->codec, opts->profile);
        strcat(cmd, tmp);
    }
    else if (strcmp(opts->codec, "h265_mi50") == 0)
    {
        strcat(cmd,
            "-c:v hevc_vaapi -rc_mode:v auto -qp 25 "
            "-profile:v main -level:v 5.1 ");
    }
    else {
        strcat(cmd, "-c:v copy ");
    }

    // deblock
    if (strcmp(opts->codec, "h265_mi50") == 0) {
        strcat(cmd, "-vf \"format=nv12,hwupload\" ");
    } else {
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

    // audio codec
    if (c->opts.use_aac_for_h265) {
    strcat(cmd, "-c:a aac -q:a 2 -ar 48000 ");
        } else {
    strcat(cmd, "-c:a pcm_s16le -ar 48000 ");
        }

    // audio normalization
    if (strcmp(opts->audio_norm, "none") == 0) {
        strcat(cmd,
            "-af \"aresample=resampler=soxr:precision=28:cheby=1\" ");
    }
    else if (strcmp(opts->audio_norm, "peak_norm") == 0) {
        strcat(cmd,
            "-af \"aresample=resampler=soxr:precision=28:cheby=1,volume=-3dB\" ");
    }
    else if (strcmp(opts->audio_norm, "peak_norm_2pass") == 0) {
        char tmp[256];
        snprintf(tmp, sizeof(tmp),
            "-af \"aresample=resampler=soxr:precision=28:cheby=1,volume=%.2fdB\" ",
            opts->gain);
        strcat(cmd, tmp);
    }
    else if (strcmp(opts->audio_norm, "loudness_norm") == 0) {
        strcat(cmd,
            "-af \"aresample=resampler=soxr:precision=28:cheby=1,"
            "loudnorm=I=-11:TP=-1.5:LRA=7\" ");
    }
    else if (strcmp(opts->audio_norm, "loudness_norm_2pass") == 0) {
        char tmp[512];
        snprintf(tmp, sizeof(tmp),
            "-af \"aresample=resampler=soxr:precision=28:cheby=1,"
            "loudnorm=I=%.1f:TP=%.1f:LRA=%.1f:"
            "measured_I=%.2f:measured_TP=%.2f:measured_LRA=%.2f:"
            "measured_thresh=%.2f:offset=%.2f:linear=true\" ",
            opts->I_target,
            opts->TP_target,
            opts->LRA_target,
            opts->measured_I,
            opts->measured_TP,
            opts->measured_LRA,
            opts->measured_thresh,
            opts->measured_offset
        );
        strcat(cmd, tmp);
    }

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

    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "%s -progress pipe:1 -nostats -nostdin 2>&1", cmd_base);

    FILE* fp = popen(cmd, "r");
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
        make_output_name(input, &c->opts, output, sizeof(output));

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
        char cmd[8192];
        if (strcmp(c->opts.codec, "h265_mi50") == 0) {
            c->opts.use_aac_for_h265 = 1;
        } else {
            c->opts.use_aac_for_h265 = 0;
        }
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
