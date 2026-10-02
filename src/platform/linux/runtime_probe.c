#include "runtime_probe.h"
#include "../runtime_probe_common.h"
#include "../runtime_catalog.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef FFMPEG_CONVERTER_SOURCE_DIR
#define FFMPEG_CONVERTER_SOURCE_DIR "."
#endif

typedef struct {
    int initialized;
    LinuxCodecSupport support;
} LinuxCodecSupportCache;

static LinuxCodecSupportCache g_cache;
static int get_process_dir(char *out_dir, size_t out_dir_sz);

/* ---------------------------------------------------------------
 *  Crash-safe subprocess runner for GPU encode probes
 * ---------------------------------------------------------------
 * Real one-frame hardware encode probes (VAAPI/Vulkan) are inherently
 * risky: on some driver/GPU combinations (observed on AMD Vega-class
 * VAAPI), rapid back-to-back device-context churn (open/encode/close,
 * repeated for every codec variant on every render node) can destabilize
 * the GPU badly enough to crash the compositor (VM_L2_PROTECTION_FAULT).
 * A later probe on the same already-wedged node then "fails" even though
 * the driver genuinely supports that profile — a false negative that is
 * really a side effect of the earlier crash, not a capability gap.
 *
 * These helpers replace plain system() for every such probe with:
 *   - fork()/exec() instead of system() — isolates a crashing ffmpeg
 *     child from the probing process itself.
 *   - A bounded wall-clock timeout — bounds worst-case hang if the GPU
 *     wedges instead of exiting.
 *   - Explicit WIFSIGNALED() detection — lets callers tell "driver
 *     cleanly refused" (exit code) apart from "child was killed by a
 *     signal" (crash), so a per-node probe loop can stop early instead
 *     of hammering an already-unstable device with more probes.
 */
#define PROBE_DEFAULT_TIMEOUT_MS 8000

typedef struct {
    int crashed;      /* 1 if killed by a signal or forcibly timed out */
    int signal_num;   /* signal number if crashed via WIFSIGNALED, else 0 */
    int timed_out;    /* 1 if we had to SIGKILL the child ourselves */
} ProbeRunInfo;

/**
 * run_probe_cmd()
 * Runs `/bin/sh -c cmd` via fork+exec, polling with WNOHANG up to
 * timeout_ms before giving up and SIGKILLing the child. Returns 1 only if
 * the child exited normally with status 0. info (optional) reports
 * whether the child crashed (signal) or had to be force-killed (timeout)
 * so callers can distinguish driver instability from a normal refusal.
 */
static int run_probe_cmd(const char *cmd, int timeout_ms, ProbeRunInfo *info)
{
    pid_t pid;
    int status = 0;
    int elapsed_ms = 0;
    const int poll_ms = 50;

    if (info) { info->crashed = 0; info->signal_num = 0; info->timed_out = 0; }
    if (!cmd || !cmd[0])
        return 0;

    pid = fork();
    if (pid < 0)
        return 0;

    if (pid == 0) {
        /* Child: cmd already carries its own I/O redirection. */
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    for (;;) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid)
            break;
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return 0;
        }
        if (timeout_ms > 0 && elapsed_ms >= timeout_ms) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            if (info) { info->crashed = 1; info->timed_out = 1; }
            return 0;
        }
        usleep(poll_ms * 1000);
        elapsed_ms += poll_ms;
    }

    if (WIFSIGNALED(status)) {
        if (info) { info->crashed = 1; info->signal_num = WTERMSIG(status); }
        return 0;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/**
 * probe_debug_enabled() / probe_log_path()
 * When FFMPEG_CONVERTER_PROBE_DEBUG is set in the environment, probe
 * stderr is appended to a log file instead of being discarded, so probe
 * failures (including driver error text) can actually be diagnosed.
 * Disabled by default to keep normal startup silent and fast.
 */
static int probe_debug_enabled(void)
{
    static int checked = 0, enabled = 0;
    if (!checked) {
        const char *v = getenv("FFMPEG_CONVERTER_PROBE_DEBUG");
        enabled = (v && v[0] && strcmp(v, "0") != 0);
        checked = 1;
    }
    return enabled;
}

static const char *probe_log_path(void)
{
    static char path[PATH_MAX];
    static int resolved = 0;
    if (!resolved) {
        const char *tmp = getenv("TMPDIR");
        snprintf(path, sizeof(path), "%s/ffmpeg_converter_probe_debug.log",
                 (tmp && tmp[0]) ? tmp : "/tmp");
        resolved = 1;
    }
    return path;
}

/* Fills redirect_out with the stdout/stderr redirection suffix to append
 * to a probe command: silent by default, or appended to the debug log
 * when FFMPEG_CONVERTER_PROBE_DEBUG is set. */
static const char *probe_redirect_suffix(char *redirect_out, size_t sz)
{
    if (probe_debug_enabled())
        snprintf(redirect_out, sz, ">/dev/null 2>>%s", probe_log_path());
    else
        snprintf(redirect_out, sz, ">/dev/null 2>&1");
    return redirect_out;
}

int linux_probe_catalog_component_enabled(const char *catalog_path,
                                          const char *platform,
                                          const char *group,
                                          const char *encoder,
                                          const char *final_codec)
{
    return runtime_catalog_component_enabled(catalog_path, platform, group,
                                             encoder, final_codec);
}

static int resolve_presets_v2(char *out_path, size_t out_path_sz)
{
    const char *presets_env = getenv("PRESETS_PATH");
    const char *env_path = getenv("PRESETS_V2_PATH");
    char process_dir[PATH_MAX];

    if (!out_path || out_path_sz == 0)
        return 0;
    out_path[0] = '\0';

    if (presets_env && presets_env[0]) {
        snprintf(out_path, out_path_sz, "%s/presets.json", presets_env);
        if (access(out_path, R_OK) == 0)
            return 1;
        copy_string(out_path, out_path_sz, presets_env);
        if (access(out_path, R_OK) == 0)
            return 1;
    }
    if (env_path && access(env_path, R_OK) == 0) {
        copy_string(out_path, out_path_sz, env_path);
        return 1;
    }
    if (get_process_dir(process_dir, sizeof(process_dir))) {
        snprintf(out_path, out_path_sz, "%s/presets.json", process_dir);
        if (access(out_path, R_OK) == 0)
            return 1;
        snprintf(out_path, out_path_sz, "%s/presets_v2.json", process_dir);
        if (access(out_path, R_OK) == 0)
            return 1;
    }
#ifdef FFMPEG_CONVERTER_SOURCE_DIR
    snprintf(out_path, out_path_sz, "%s/build/generated/presets.json",
             FFMPEG_CONVERTER_SOURCE_DIR);
    if (access(out_path, R_OK) == 0)
        return 1;
    snprintf(out_path, out_path_sz, "%s/build/generated/presets_v2.json",
             FFMPEG_CONVERTER_SOURCE_DIR);
    if (access(out_path, R_OK) == 0)
        return 1;
#endif
    out_path[0] = '\0';
    return 0;
}

/**
 * posix_shell_quote()
 * Returns a malloc'd single-quoted shell-safe string for path.
 * Embedded single-quotes are replaced with '\''.
 * Caller must free() the returned pointer.
 * Returns NULL on allocation failure.
 */
static char *posix_shell_quote(const char *path)
{
    size_t in_len;
    char *out;
    char *p;
    size_t i;

    if (!path) return NULL;
    in_len = strlen(path);
    /* worst case: each char becomes '\'', plus outer single-quotes + NUL */
    out = malloc(2 + in_len * 4 + 1);
    if (!out) return NULL;

    p = out;
    *p++ = '\'';
    for (i = 0; i < in_len; i++) {
        if (path[i] == '\'') {
            *p++ = '\'';
            *p++ = '\\';
            *p++ = '\'';
            *p++ = '\'';
        } else {
            *p++ = path[i];
        }
    }
    *p++ = '\'';
    *p   = '\0';
    return out;
}

static int is_executable_file(const char *path)
{
    return path && path[0] != '\0' && access(path, X_OK) == 0;
}

static int get_process_dir(char *out_dir, size_t out_dir_sz)
{
    char exe_path[PATH_MAX];
    ssize_t len;
    char *last_slash;

    if (!out_dir || out_dir_sz == 0)
        return 0;

    len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len < 0)
        return 0;

    exe_path[len] = '\0';
    last_slash = strrchr(exe_path, '/');
    if (!last_slash)
        return 0;

    *last_slash = '\0';
    copy_string(out_dir, out_dir_sz, exe_path);
    return 1;
}

static int try_bundled_candidate(const char *base_dir,
                                 const char *relative_path,
                                 const char *name,
                                 char *out_path,
                                 size_t out_path_sz)
{
    char candidate[PATH_MAX];

    if (!base_dir || base_dir[0] == '\0' || !relative_path || !name)
        return 0;

    snprintf(candidate, sizeof(candidate), "%s/%s/%s", base_dir, relative_path, name);
    if (!is_executable_file(candidate))
        return 0;

    copy_string(out_path, out_path_sz, candidate);
    return 1;
}

static int resolve_bundled_binary(const char *name, char *out_path, size_t out_path_sz)
{
    const char *appdir_env;
    char process_dir[PATH_MAX];

    if (!name || !out_path || out_path_sz == 0)
        return 0;

    appdir_env = getenv("APPDIR");
    if (appdir_env && appdir_env[0] != '\0') {
        if (try_bundled_candidate(appdir_env, "usr/bin", name, out_path, out_path_sz))
            return 1;
        if (try_bundled_candidate(appdir_env, "bin", name, out_path, out_path_sz))
            return 1;
    }

    if (get_process_dir(process_dir, sizeof(process_dir))) {
        if (try_bundled_candidate(process_dir, "", name, out_path, out_path_sz))
            return 1;
        if (try_bundled_candidate(process_dir, "bin", name, out_path, out_path_sz))
            return 1;
    }

#ifdef FFMPEG_CONVERTER_SOURCE_DIR
    if (try_bundled_candidate(FFMPEG_CONVERTER_SOURCE_DIR,
                              "src/platform/linux/bin",
                              name,
                              out_path,
                              out_path_sz)) {
        return 1;
    }
#endif

    return 0;
}

static int resolve_path_binary(const char *name, char *out_path, size_t out_path_sz)
{
    const char *path_env;
    char path_copy[8192];
    char *dir;
    char *saveptr = NULL;

    if (!name || !out_path || out_path_sz == 0)
        return 0;

    path_env = getenv("PATH");
    if (!path_env || path_env[0] == '\0')
        return 0;

    copy_string(path_copy, sizeof(path_copy), path_env);
    dir = strtok_r(path_copy, ":", &saveptr);
    while (dir) {
        char candidate[PATH_MAX];

        if (dir[0] != '\0') {
            snprintf(candidate, sizeof(candidate), "%s/%s", dir, name);
            if (is_executable_file(candidate)) {
                copy_string(out_path, out_path_sz, candidate);
                return 1;
            }
        }

        dir = strtok_r(NULL, ":", &saveptr);
    }

    return 0;
}

/* ---------------------------------------------------------------
 *  Binary resolution — STRICT BUNDLED-ONLY FOR FFMPEG/FFPROBE
 * ---------------------------------------------------------------
 *
 * Rule: ffmpeg and ffprobe MUST be bundled (same folder as utility).
 * No environment override, no system PATH fallback.
 *
 * mkvmerge and MP4Box MAY be system-installed (checked via PATH).
 * ---------------------------------------------------------------
 */

/* Strict bundled-only resolver: no env, no PATH, no fallback.
 * Used for ffmpeg and ffprobe. */
static void resolve_bundled_only(const char* binary_name,
                                 char* out_path,
                                 size_t out_path_sz,
                                 int* using_bundled)
{
    if (using_bundled)
        *using_bundled = 0;

    if (resolve_bundled_binary(binary_name, out_path, out_path_sz)) {
        if (using_bundled)
            *using_bundled = 1;
        return;
    }

    /* Bundled binary not found → empty path signals failure */
    out_path[0] = '\0';
}

/* Flexible resolver with optional system fallback.
 * Used for mkvmerge and MP4Box (env override + bundled + PATH). */
static void resolve_preferred_binary(const char* env_name_primary,
                                     const char* env_name_secondary,
                                     const char* binary_name,
                                     char* out_path,
                                     size_t out_path_sz,
                                     int* using_bundled,
                                     int allow_system_fallback)
{
    const char* env_path;

    if (using_bundled)
        *using_bundled = 0;

    /* Env override check (honored for all binaries) */
    env_path = env_name_primary ? getenv(env_name_primary) : NULL;
    if (is_executable_file(env_path)) {
        copy_string(out_path, out_path_sz, env_path);
        return;
    }

    env_path = env_name_secondary ? getenv(env_name_secondary) : NULL;
    if (is_executable_file(env_path)) {
        copy_string(out_path, out_path_sz, env_path);
        return;
    }

    /* Bundled binary check (always tried) */
    if (resolve_bundled_binary(binary_name, out_path, out_path_sz)) {
        if (using_bundled)
            *using_bundled = 1;
        return;
    }

    if (allow_system_fallback) {
        /* System PATH fallback — allowed for mkvmerge/MP4Box */
        if (resolve_path_binary(binary_name, out_path, out_path_sz))
            return;
        /* Final fallback — raw binary name */
        copy_string(out_path, out_path_sz, binary_name);
    } else {
        /* Strict mode: bundled not found → empty path */
        out_path[0] = '\0';
        }
}

/**
 * probe_simple_encoder()
 * Tests a single GPU encoder (NVENC, AMF, QSV) via a one-frame encode.
 * No device path is required — these encoders auto-select the GPU.
 * Returns 1 if the encoder is available, 0 otherwise.
 */
static int probe_simple_encoder(const char *ffmpeg_bin,
                                const char *encoder_name)
{
    char cmd[8192];
    char redirect[160];
    char *q;

    if (!ffmpeg_bin || ffmpeg_bin[0] == '\0' || !encoder_name)
        return 0;

    q = posix_shell_quote(ffmpeg_bin);
    if (!q) return 0;

    snprintf(cmd, sizeof(cmd),
             "%s -v error -hide_banner "
             "-f lavfi -i color=size=1920x1080:rate=1 "
             "-frames:v 1 "
             "-c:v %s -f null - %s",
             q, encoder_name, probe_redirect_suffix(redirect, sizeof(redirect)));
    free(q);

    return run_probe_cmd(cmd, PROBE_DEFAULT_TIMEOUT_MS, NULL);
}

static int probe_simple_encoder_format(const char *ffmpeg_bin,
                                       const char *encoder_name,
                                       const char *pixel_format,
                                       const char *extra_args)
{
    char cmd[8192];
    char redirect[160];
    char *q;

    if (!ffmpeg_bin || !encoder_name || !pixel_format)
        return 0;
    q = posix_shell_quote(ffmpeg_bin);
    if (!q)
        return 0;
    snprintf(cmd, sizeof(cmd),
             "%s -v error -hide_banner -f lavfi "
             "-i color=size=1920x1080:rate=1,format=%s "
             "-frames:v 1 -vf format=%s %s-c:v %s -f null - %s",
             q, pixel_format, pixel_format,
             extra_args ? extra_args : "", encoder_name,
             probe_redirect_suffix(redirect, sizeof(redirect)));
    free(q);
    return run_probe_cmd(cmd, PROBE_DEFAULT_TIMEOUT_MS, NULL);
}

/* Forward declaration: skips CPU-only (llvmpipe/lavapipe) Vulkan devices.
 * Defined below; needed here so probe_vulkan_prores() can use the same
 * software-device filter as probe_vulkan_encoder(). */
static int vulkan_device_is_software(int device_index);

/**
 * probe_vulkan_prores()
 * Tests prores_ks_vulkan on vk:0 through vk:7.
 * Scans all devices, records a working_mask bitmask and device_count.
 * Returns the highest working device index (statistically more likely
 * to be a discrete GPU), or -1 if no device passes.
 */
#define LINUX_VULKAN_MAX_DEVICES 8

static int probe_vulkan_prores(const char *ffmpeg_bin,
                               int *out_working_mask,
                               int *out_device_count)
{
    int i, mask = 0, count = 0, best = -1;
    char *q;

    if (out_working_mask)  *out_working_mask  = 0;
    if (out_device_count)  *out_device_count  = 0;

    if (!ffmpeg_bin || ffmpeg_bin[0] == '\0') return -1;

    q = posix_shell_quote(ffmpeg_bin);
    if (!q) return -1;

    for (i = 0; i < LINUX_VULKAN_MAX_DEVICES; i++) {
        char cmd[8192];
        char redirect[160];
        ProbeRunInfo info;

        if (vulkan_device_is_software(i))
            continue;

        snprintf(cmd, sizeof(cmd),
                 "%s -v error -hide_banner "
                 "-init_hw_device vulkan=vk:%d -filter_hw_device vk "
                 "-f lavfi -i color=size=1920x1080:rate=1 "
                 "-frames:v 1 "
                 "-vf format=yuv422p10le,hwupload "
                 "-c:v prores_ks_vulkan -f null - %s",
                 q, i, probe_redirect_suffix(redirect, sizeof(redirect)));

        if (run_probe_cmd(cmd, PROBE_DEFAULT_TIMEOUT_MS, &info)) {
            mask |= (1 << i);
            best = i;
            count++;
        } else if (info.crashed) {
            /* Driver crashed/hung on this device — don't keep hammering
             * an unstable GPU context with more probes this run. */
            break;
        } else if (count == 0 && i >= 2) {
            /* No successes after 3 attempts — no Vulkan GPU present */
            break;
        }
    }

    free(q);

    if (out_working_mask)  *out_working_mask  = mask;
    if (out_device_count)  *out_device_count  = count;
    return best;
}

/**
 * ffmpeg_has_encoder()
 * Cheap pre-filter: checks `ffmpeg -encoders` output for encoder_name before
 * running an expensive one-frame probe. Keeps startup time flat on systems
 * where the encoder is not present in the bundled ffmpeg build at all.
 */
static int ffmpeg_has_encoder(const char *ffmpeg_bin, const char *encoder_name)
{
    char cmd[1024];
    char *q;
    char line[1024];
    FILE *fp;
    int found = 0;
    size_t name_len;

    if (!ffmpeg_bin || ffmpeg_bin[0] == '\0' || !encoder_name)
        return 0;

    q = posix_shell_quote(ffmpeg_bin);
    if (!q) return 0;

    snprintf(cmd, sizeof(cmd), "%s -hide_banner -v error -encoders 2>/dev/null", q);
    free(q);

    fp = popen(cmd, "r");
    if (!fp) return 0;

    name_len = strlen(encoder_name);
    while (fgets(line, sizeof(line), fp)) {
        char *pos = strstr(line, encoder_name);
        if (pos && (pos == line || pos[-1] == ' ') &&
            (pos[name_len] == ' ' || pos[name_len] == '\n' || pos[name_len] == '\0')) {
            found = 1;
            break;
        }
    }
    pclose(fp);
    return found;
}

/**
 * vulkan_device_is_software()
 * Fixes the known llvmpipe issue: a CPU-only "software" Vulkan device
 * (Mesa lavapipe/llvmpipe) can otherwise report a "working" encoder in the
 * one-frame probe, which is never usable in practice. Parses `vulkaninfo`
 * device listing and skips devices whose name/type indicates a software
 * implementation. If vulkaninfo is unavailable, fails open (returns 0) —
 * the one-frame probe itself remains the final authority.
 */
static int vulkan_device_is_software(int device_index)
{
    FILE *fp;
    char line[1024];
    int current_index = -1;
    int result = 0;

    fp = popen("vulkaninfo --summary 2>/dev/null", "r");
    if (!fp)
        return 0;

    while (fgets(line, sizeof(line), fp)) {
        /* vulkaninfo --summary prints one "GPU<N>:" heading per device,
         * followed by indented "deviceName" and "deviceType" fields. */
        int scanned_index;
        if (sscanf(line, " GPU%d :", &scanned_index) == 1 ||
            sscanf(line, " GPU%d:", &scanned_index) == 1) {
            current_index = scanned_index;
            continue;
        }
        if (current_index != device_index)
            continue;

        if ((strstr(line, "deviceType") && strstr(line, "CPU")) ||
            strstr(line, "llvmpipe") ||
            strstr(line, "lavapipe")) {
            result = 1;
            break;
        }
    }
    pclose(fp);
    return result;
}

/**
 * probe_vulkan_encoder()
 * Generic hardware Vulkan video encoder probe (h264_vulkan, hevc_vulkan,
 * av1_vulkan), modeled on probe_vulkan_prores(). Scans vk:0..7, skipping
 * devices identified as software (llvmpipe/lavapipe) by
 * vulkan_device_is_software(). Returns the highest working device index,
 * or -1 if no device passes.
 */
static int probe_vulkan_encoder(const char *ffmpeg_bin,
                                const char *encoder_name,
                                int *out_working_mask,
                                int *out_device_count)
{
    int i, mask = 0, count = 0, best = -1;
    char *q;

    if (out_working_mask)  *out_working_mask  = 0;
    if (out_device_count)  *out_device_count  = 0;

    if (!ffmpeg_bin || ffmpeg_bin[0] == '\0' || !encoder_name) return -1;

    q = posix_shell_quote(ffmpeg_bin);
    if (!q) return -1;

    for (i = 0; i < LINUX_VULKAN_MAX_DEVICES; i++) {
        char cmd[8192];
        char redirect[160];
        ProbeRunInfo info;

        if (vulkan_device_is_software(i))
            continue;

        snprintf(cmd, sizeof(cmd),
                 "%s -v error -hide_banner "
                 "-init_hw_device vulkan=vk:%d -filter_hw_device vk "
                 "-f lavfi -i color=size=1920x1080:rate=1 "
                 "-frames:v 1 "
                 "-vf format=nv12,hwupload "
                 "-c:v %s -f null - %s",
                 q, i, encoder_name, probe_redirect_suffix(redirect, sizeof(redirect)));

        if (run_probe_cmd(cmd, PROBE_DEFAULT_TIMEOUT_MS, &info)) {
            mask |= (1 << i);
            best = i;
            count++;
        } else if (info.crashed) {
            /* Driver crashed/hung on this device — stop scanning further
             * devices for this encoder this run rather than risk cascading
             * failures on an already-unstable GPU context. */
            break;
        } else if (count == 0 && i >= 2) {
            /* No successes after 3 attempts — no working Vulkan encode GPU */
            break;
        }
    }

    free(q);

    if (out_working_mask)  *out_working_mask  = mask;
    if (out_device_count)  *out_device_count  = count;
    return best;
}

/**
 * vaapi_real_encode_probe_enabled()
 * SAFETY POLICY: real one-frame VAAPI encode probes are OPT-IN ONLY,
 * via FFMPEG_CONVERTER_VAAPI_REAL_PROBE=1.
 *
 * Postmortem evidence (kernel log, this repo's incident): a real VAAPI
 * encode probe on renderD128/129 submits GPU-ring commands on the same
 * physical adapter (amdgpu 0000:bN:00.0) that the Wayland compositor is
 * concurrently rendering on. This caused a genuine kernel-level
 * VM_L2_PROTECTION_FAULT / gfx-ring timeout, which Mesa/mutter's own
 * fault handling treated as fatal — gnome-shell itself (not our ffmpeg
 * child) aborted and dumped core. fork()/timeout/signal-detection
 * isolation (see run_probe_cmd()) only prevents OUR process from
 * cascading more probes after such a crash; it cannot prevent the fault,
 * because the race is at the GPU hardware/ring level, outside any
 * userspace process boundary.
 *
 * Default (safe): rely solely on the read-only `vainfo` profile listing
 * (vaapi_node_profile_text()/vaapi_profile_listed()) as the final
 * authority. This cannot crash anything — it never allocates a surface,
 * uploads a frame, or touches the GPU ring — at the cost of occasionally
 * reporting a profile as "supported" when some other factor (firmware,
 * VRAM, permissions) would still make a real encode fail.
 *
 * Opt-in (stricter, riskier): set FFMPEG_CONVERTER_VAAPI_REAL_PROBE=1 to
 * additionally confirm with the real crash-isolated one-frame encode
 * (run_probe_cmd() + timeout + WIFSIGNALED detection) for profiles the
 * vainfo listing already reports as present. Never runs for profiles
 * vainfo doesn't list.
 */
static int vaapi_real_encode_probe_enabled(void)
{
    static int checked = 0, enabled = 0;
    if (!checked) {
        const char *v = getenv("FFMPEG_CONVERTER_VAAPI_REAL_PROBE");
        enabled = (v && v[0] && strcmp(v, "0") != 0);
        checked = 1;
    }
    return enabled;
}

/**
 * vaapi_profile_supported()
 * Resolves whether a VAAPI profile should be reported as supported, per
 * the safety policy in vaapi_real_encode_probe_enabled(): vainfo listing
 * is authoritative by default; the real encode only runs as an opt-in
 * extra confirmation, and only when vainfo already says the profile is
 * present. If vainfo itself is unavailable (profile_text is NULL), this
 * fails CLOSED (returns 0 / "unsupported") rather than falling back to an
 * unconditional real encode — a missing vainfo should never silently
 * re-enable the risky path.
 *
 * real_probe_fn is called with real_probe_arg only when an opt-in
 * confirmation encode is actually needed; out_info receives its crash
 * status so the caller can still abort remaining checks on this node.
 */
typedef int (*VaapiRealProbeFn)(void *arg, ProbeRunInfo *out_info);

/* Forward declaration: defined after vaapi_node_profile_text() below. */
static int vaapi_profile_listed(const char *profile_text, const char *profile_entrypoint);

static int vaapi_profile_supported(const char *profile_text,
                                   const char *profile_entrypoint,
                                   VaapiRealProbeFn real_probe_fn,
                                   void *real_probe_arg,
                                   ProbeRunInfo *out_info)
{
    int listed = vaapi_profile_listed(profile_text, profile_entrypoint);

    if (out_info) { out_info->crashed = 0; out_info->signal_num = 0; out_info->timed_out = 0; }

    if (listed <= 0)
        return 0;  /* not listed, or vainfo unavailable — fail closed */

    if (!vaapi_real_encode_probe_enabled())
        return 1;  /* safe default: vainfo listing is authoritative */

    return real_probe_fn(real_probe_arg, out_info);
}

/**
 * vaapi_node_profile_text()
 * Read-only capability pre-filter: runs `vainfo --display drm --device
 * <render_node> -a` ONCE per render node (no surface/frame allocation, no
 * encode) and returns its captured text, or NULL if vainfo is unavailable
 * or produced no output. The result is only used to SKIP a real encode
 * attempt when the profile is definitely absent; it never upgrades a
 * result, so the real one-frame encode below remains the final authority
 * — mirroring the existing vulkan_device_is_software() pre-filter pattern.
 * Caller must free() the returned buffer.
 */
static char *vaapi_node_profile_text(const char *render_node)
{
    char cmd[512];
    char *q_node;
    char *buf;
    size_t cap = 16384, len = 0;
    FILE *fp;

    if (!render_node) return NULL;
    q_node = posix_shell_quote(render_node);
    if (!q_node) return NULL;

    snprintf(cmd, sizeof(cmd),
             "vainfo --display drm --device %s -a 2>/dev/null", q_node);
    free(q_node);

    fp = popen(cmd, "r");
    if (!fp) return NULL;

    buf = malloc(cap);
    if (!buf) { pclose(fp); return NULL; }
    buf[0] = '\0';

    {
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            size_t line_len = strlen(line);
            if (len + line_len + 1 > cap) {
                size_t new_cap = cap * 2;
                char *grown = realloc(buf, new_cap);
                if (!grown) break;
                buf = grown;
                cap = new_cap;
            }
            memcpy(buf + len, line, line_len + 1);
            len += line_len;
        }
    }
    pclose(fp);

    if (len == 0) { free(buf); return NULL; }
    return buf;
}

/* Returns 1 if profile_entrypoint (e.g. "VAProfileHEVCMain10/VAEntrypointEncSlice")
 * appears in the vainfo text captured by vaapi_node_profile_text(), 0 if the
 * text exists but doesn't mention it, or -1 if profile_text is NULL (vainfo
 * unavailable/failed — "unknown", caller should fail open to the real probe). */
static int vaapi_profile_listed(const char *profile_text, const char *profile_entrypoint)
{
    if (!profile_text) return -1;
    return strstr(profile_text, profile_entrypoint) != NULL;
}

static int probe_vaapi_encoder(const char *ffmpeg_bin,
                               const char *render_node,
                               const char *encoder_name,
                               ProbeRunInfo *out_info)
{
    char cmd[8192];
    char redirect[160];
    char *q;
    char *q_node;

    if (!ffmpeg_bin || !render_node || !encoder_name)
        return 0;

    q = posix_shell_quote(ffmpeg_bin);
    if (!q) return 0;

    q_node = posix_shell_quote(render_node);
    if (!q_node) { free(q); return 0; }

    snprintf(cmd,
             sizeof(cmd),
             "%s -v error -hide_banner "
             "-init_hw_device vaapi=va:%s -filter_hw_device va "
             "-f lavfi -i color=size=1920x1080:rate=1 "
             "-frames:v 1 -vf format=nv12,hwupload "
             "-c:v %s -f null - %s",
             q,
             q_node,
             encoder_name,
             probe_redirect_suffix(redirect, sizeof(redirect)));
    free(q);
    free(q_node);

    return run_probe_cmd(cmd, PROBE_DEFAULT_TIMEOUT_MS, out_info);
}

static int probe_vaapi_encoder_format(const char *ffmpeg_bin,
                                      const char *render_node,
                                      const char *encoder_name,
                                      const char *pixel_format,
                                      const char *extra_args,
                                      ProbeRunInfo *out_info)
{
    char cmd[8192];
    char redirect[160];
    char *q;
    char *q_node;

    if (!ffmpeg_bin || !render_node || !encoder_name || !pixel_format)
        return 0;
    q = posix_shell_quote(ffmpeg_bin);
    q_node = posix_shell_quote(render_node);
    if (!q || !q_node) {
        free(q);
        free(q_node);
        return 0;
    }
    snprintf(cmd, sizeof(cmd),
             "%s -v error -hide_banner "
             "-init_hw_device vaapi=va:%s -filter_hw_device va "
             "-f lavfi -i color=size=1920x1080:rate=1,format=%s "
             "-frames:v 1 -vf format=%s,hwupload %s-c:v %s -f null - %s",
             q, q_node, pixel_format, pixel_format,
             extra_args ? extra_args : "", encoder_name,
             probe_redirect_suffix(redirect, sizeof(redirect)));
    free(q);
    free(q_node);
    return run_probe_cmd(cmd, PROBE_DEFAULT_TIMEOUT_MS, out_info);
}

/* ---------------------------------------------------------------
 *  Persistent disk cache for hardware probe results
 * ---------------------------------------------------------------
 * The real encode probes above are now crash-isolated and pre-filtered,
 * but the single biggest remaining way to cut GPU churn (and therefore
 * crash exposure) is to simply not repeat them on every launch. Results
 * are persisted keyed by a signature (ffmpeg binary + presets.json
 * stamps, render-node set); any mismatch (ffmpeg rebuilt/updated, catalog
 * changed, GPU plugged/unplugged) invalidates the cache automatically.
 * linux_invalidate_codec_support_cache() lets a future "Rescan hardware"
 * GUI/CLI action force a fresh probe on demand.
 */
#define PROBE_CACHE_MAGIC 0x46435032u /* "FCP2" */

typedef struct {
    unsigned magic;
    unsigned struct_size;
    char signature[512];
} ProbeCacheHeader;

static int get_cache_dir(char *out_dir, size_t out_dir_sz)
{
    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");

    if (xdg && xdg[0]) {
        snprintf(out_dir, out_dir_sz, "%s/ffmpeg_converter", xdg);
        return 1;
    }
    if (home && home[0]) {
        snprintf(out_dir, out_dir_sz, "%s/.cache/ffmpeg_converter", home);
        return 1;
    }
    return 0;
}

static int get_cache_file_path(char *out_path, size_t out_path_sz)
{
    char dir[PATH_MAX];

    if (!get_cache_dir(dir, sizeof(dir)))
        return 0;
    snprintf(out_path, out_path_sz, "%s/hw_probe_cache.bin", dir);
    return 1;
}

static void append_file_stamp(char *sig, size_t sig_sz, const char *path)
{
    struct stat st;
    size_t len = strlen(sig);

    if (path && path[0] && stat(path, &st) == 0) {
        snprintf(sig + len, sig_sz - len, "|%s:%lld:%lld",
                 path, (long long)st.st_mtime, (long long)st.st_size);
    } else {
        snprintf(sig + len, sig_sz - len, "|%s:missing", path ? path : "");
    }
}

static void compute_probe_signature(char *sig, size_t sig_sz,
                                    const char *ffmpeg_bin,
                                    const char *catalog_path)
{
    DIR *dir;
    struct dirent *entry;
    char names[512] = {0};

    snprintf(sig, sig_sz, "v2");
    append_file_stamp(sig, sig_sz, ffmpeg_bin);
    append_file_stamp(sig, sig_sz, catalog_path);

    /* Plugging/unplugging a GPU changes the render-node set — invalidate
     * the cache rather than reuse results captured on different hardware. */
    dir = opendir("/dev/dri");
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            if (starts_with(entry->d_name, "renderD")) {
                size_t len = strlen(names);
                snprintf(names + len, sizeof(names) - len, ",%s", entry->d_name);
            }
        }
        closedir(dir);
    }
    {
        size_t len = strlen(sig);
        snprintf(sig + len, sig_sz - len, "|nodes:%s", names);
    }
}

static int load_probe_cache(const char *cache_path, const char *expected_sig,
                            LinuxCodecSupport *out)
{
    FILE *fp;
    ProbeCacheHeader hdr;
    size_t n;

    fp = fopen(cache_path, "rb");
    if (!fp) return 0;

    n = fread(&hdr, 1, sizeof(hdr), fp);
    if (n != sizeof(hdr) ||
        hdr.magic != PROBE_CACHE_MAGIC ||
        hdr.struct_size != (unsigned)sizeof(*out) ||
        strncmp(hdr.signature, expected_sig, sizeof(hdr.signature)) != 0) {
        fclose(fp);
        return 0;
    }

    n = fread(out, 1, sizeof(*out), fp);
    fclose(fp);
    return n == sizeof(*out);
}

static void save_probe_cache(const char *cache_path, const char *signature,
                             const LinuxCodecSupport *support)
{
    char dir[PATH_MAX];
    char *last_slash;
    FILE *fp;
    ProbeCacheHeader hdr;

    copy_string(dir, sizeof(dir), cache_path);
    last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(dir, 0700);  /* best-effort; ignore EEXIST/errors */
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = PROBE_CACHE_MAGIC;
    hdr.struct_size = (unsigned)sizeof(*support);
    copy_string(hdr.signature, sizeof(hdr.signature), signature);

    fp = fopen(cache_path, "wb");
    if (!fp) return;
    fwrite(&hdr, 1, sizeof(hdr), fp);
    fwrite(support, 1, sizeof(*support), fp);
    fclose(fp);
}

/* Adapter args/trampolines bridging VaapiRealProbeFn to the existing
 * probe_vaapi_encoder()/probe_vaapi_encoder_format() signatures, used only
 * for the opt-in FFMPEG_CONVERTER_VAAPI_REAL_PROBE=1 confirmation path. */
typedef struct {
    const char *ffmpeg_bin;
    const char *render_node;
    const char *encoder_name;
} VaapiSimpleProbeArgs;

static int vaapi_simple_probe_trampoline(void *arg, ProbeRunInfo *out_info)
{
    VaapiSimpleProbeArgs *a = (VaapiSimpleProbeArgs *)arg;
    return probe_vaapi_encoder(a->ffmpeg_bin, a->render_node, a->encoder_name, out_info);
}

typedef struct {
    const char *ffmpeg_bin;
    const char *render_node;
    const char *encoder_name;
    const char *pixel_format;
    const char *extra_args;
} VaapiFormatProbeArgs;

static int vaapi_format_probe_trampoline(void *arg, ProbeRunInfo *out_info)
{
    VaapiFormatProbeArgs *a = (VaapiFormatProbeArgs *)arg;
    return probe_vaapi_encoder_format(a->ffmpeg_bin, a->render_node, a->encoder_name,
                                      a->pixel_format, a->extra_args, out_info);
}

int linux_probe_codec_support(LinuxCodecSupport *out_support)
{
    LinuxCodecSupport detected;
    DIR *dir;
    struct dirent *entry;
    char catalog_path[PATH_MAX];
    int vaapi_h264_enabled;
    int vaapi_hevc_enabled;
    int vaapi_av1_enabled;
    int vaapi_hevc_10bit_enabled;
    int vaapi_av1_10bit_enabled;
    int qsv_av1_enabled;
    int qsv_hevc_10bit_enabled;
    int qsv_av1_10bit_enabled;

    if (g_cache.initialized) {
        if (out_support)
            *out_support = g_cache.support;
        return 1;
    }

    memset(&detected, 0, sizeof(detected));

    /* FFMPEG/FFPROBE: STRICT bundled-only — no env, no PATH */
    resolve_bundled_only("ffmpeg",
                         detected.ffmpeg_bin,
                         sizeof(detected.ffmpeg_bin),
                         &detected.using_bundled_ffmpeg);
    resolve_bundled_only("ffprobe",
                         detected.ffprobe_bin,
                         sizeof(detected.ffprobe_bin),
                         &detected.using_bundled_ffprobe);

    /* MKVMERGE/MP4BOX: flexible — envOverride → bundled → PATH */
    resolve_preferred_binary("MKVMERGE_BIN", NULL, "mkvmerge",
                             detected.mkvmerge_bin,
                             sizeof(detected.mkvmerge_bin),
                             &detected.using_bundled_mkvmerge,
                             1);  /* system fallback allowed */
    resolve_preferred_binary("MP4BOX_BIN", NULL, "MP4Box",
                             detected.mp4box_bin,
                             sizeof(detected.mp4box_bin),
                             &detected.using_bundled_mp4box,
                             1);  /* system fallback allowed */

    if (!resolve_presets_v2(catalog_path, sizeof(catalog_path)))
        catalog_path[0] = '\0';

    /* Persistent disk cache — the whole point of the fixes above is to
     * reduce GPU-probe churn; the biggest remaining lever is simply not
     * re-running the real encode probes on every single launch. If a
     * cache entry exists whose signature (ffmpeg binary + presets.json
     * stamps, plus the current /dev/dri render-node set) matches this
     * run, reuse its GPU capability results and skip the churn entirely.
     * Binary-path fields are always kept fresh (cheap, no GPU risk). */
    {
        char cache_path[PATH_MAX];
        char signature[768];
        LinuxCodecSupport cached;

        compute_probe_signature(signature, sizeof(signature),
                                detected.ffmpeg_bin, catalog_path);

        if (get_cache_file_path(cache_path, sizeof(cache_path)) &&
            load_probe_cache(cache_path, signature, &cached)) {
            cached.using_bundled_ffmpeg   = detected.using_bundled_ffmpeg;
            cached.using_bundled_ffprobe  = detected.using_bundled_ffprobe;
            cached.using_bundled_mkvmerge = detected.using_bundled_mkvmerge;
            cached.using_bundled_mp4box   = detected.using_bundled_mp4box;
            copy_string(cached.ffmpeg_bin, sizeof(cached.ffmpeg_bin), detected.ffmpeg_bin);
            copy_string(cached.ffprobe_bin, sizeof(cached.ffprobe_bin), detected.ffprobe_bin);
            copy_string(cached.mkvmerge_bin, sizeof(cached.mkvmerge_bin), detected.mkvmerge_bin);
            copy_string(cached.mp4box_bin, sizeof(cached.mp4box_bin), detected.mp4box_bin);

            g_cache.support = cached;
            g_cache.initialized = 1;
            if (out_support)
                *out_support = cached;
            return 1;
        }
    }

    vaapi_h264_enabled = runtime_catalog_component_enabled(catalog_path, "linux", "vaapi",
                                                   "h264", "h264_vaapi");
    vaapi_hevc_enabled = runtime_catalog_component_enabled(catalog_path, "linux", "vaapi",
                                                   "hevc", "hevc_vaapi");
    vaapi_av1_enabled = runtime_catalog_component_enabled(catalog_path, "linux", "vaapi",
                                                   "av1", "av1_vaapi");
    vaapi_hevc_10bit_enabled = runtime_catalog_component_enabled(catalog_path, "linux", "vaapi",
                                                   "hevc_10bit", "hevc_vaapi");
    vaapi_av1_10bit_enabled = runtime_catalog_component_enabled(catalog_path, "linux", "vaapi",
                                                   "av1_10bit", "av1_vaapi");
    qsv_av1_enabled = runtime_catalog_component_enabled(catalog_path, "linux", "qsv",
                                                   "av1", "av1_qsv");
    qsv_hevc_10bit_enabled = runtime_catalog_component_enabled(catalog_path, "linux", "qsv",
                                                   "hevc_10bit", "hevc_qsv");
    qsv_av1_10bit_enabled = runtime_catalog_component_enabled(catalog_path, "linux", "qsv",
                                                   "av1_10bit", "av1_qsv");

    dir = opendir("/dev/dri");
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            char render_node[PATH_MAX];
            int has_h264;
            int has_hevc;
            char *profile_text;
            int node_unstable = 0;
            ProbeRunInfo info;
            VaapiSimpleProbeArgs simple_args;
            VaapiFormatProbeArgs format_args;

            if (!starts_with(entry->d_name, "renderD"))
                continue;

            snprintf(render_node, sizeof(render_node), "/dev/dri/%s", entry->d_name);
            if (access(render_node, R_OK | W_OK) != 0)
                continue;

            /* Read-only capability check (no surface/frame allocation, no
             * GPU-ring commands submitted — cannot crash anything): runs
             * `vainfo` once per render node. By default this listing is
             * the FINAL authority for VAAPI capability detection; see
             * vaapi_real_encode_probe_enabled() for why the real one-frame
             * encode is opt-in only (postmortem: it crashed gnome-shell's
             * own compositor via a shared-GPU-ring race, not something a
             * process boundary can fence off). */
            profile_text = vaapi_node_profile_text(render_node);

            simple_args.ffmpeg_bin = detected.ffmpeg_bin;
            simple_args.render_node = render_node;

            has_h264 = 0;
            if (vaapi_h264_enabled) {
                simple_args.encoder_name = "h264_vaapi";
                has_h264 = vaapi_profile_supported(profile_text,
                                                   "VAProfileH264Main/VAEntrypointEncSlice",
                                                   vaapi_simple_probe_trampoline, &simple_args, &info);
                if (info.crashed) node_unstable = 1;
            }

            has_hevc = 0;
            if (!node_unstable && vaapi_hevc_enabled) {
                simple_args.encoder_name = "hevc_vaapi";
                has_hevc = vaapi_profile_supported(profile_text,
                                                   "VAProfileHEVCMain/VAEntrypointEncSlice",
                                                   vaapi_simple_probe_trampoline, &simple_args, &info);
                if (info.crashed) node_unstable = 1;
            }

            if (!node_unstable && vaapi_av1_enabled) {
                simple_args.encoder_name = "av1_vaapi";
                if (vaapi_profile_supported(profile_text,
                                            "VAProfileAV1Main/VAEntrypointEncSlice",
                                            vaapi_simple_probe_trampoline, &simple_args, &info))
                    detected.has_av1_vaapi = 1;
                if (info.crashed) node_unstable = 1;
            }
            if (!node_unstable && vaapi_hevc_10bit_enabled) {
                format_args.ffmpeg_bin = detected.ffmpeg_bin;
                format_args.render_node = render_node;
                format_args.encoder_name = "hevc_vaapi";
                format_args.pixel_format = "p010le";
                format_args.extra_args = "-profile:v main10 ";
                if (vaapi_profile_supported(profile_text,
                                            "VAProfileHEVCMain10/VAEntrypointEncSlice",
                                            vaapi_format_probe_trampoline, &format_args, &info))
                    detected.has_hevc_vaapi_10bit = 1;
                if (info.crashed) node_unstable = 1;
            }
            if (!node_unstable && vaapi_av1_10bit_enabled) {
                format_args.ffmpeg_bin = detected.ffmpeg_bin;
                format_args.render_node = render_node;
                format_args.encoder_name = "av1_vaapi";
                format_args.pixel_format = "p010le";
                format_args.extra_args = "";
                if (vaapi_profile_supported(profile_text,
                                            "VAProfileAV1Main10/VAEntrypointEncSlice",
                                            vaapi_format_probe_trampoline, &format_args, &info))
                    detected.has_av1_vaapi_10bit = 1;
                if (info.crashed) node_unstable = 1;
            }

            /* A crash mid-sequence (only reachable in the opt-in real-probe
             * mode) leaves later/untested codecs on this node at their safe
             * default (0 = unsupported) rather than risking further probes
             * against an already-unstable GPU context this run. They'll be
             * re-evaluated next probe run. */

            if (!detected.default_render_node[0] && (has_h264 || has_hevc)) {
                copy_string(detected.default_render_node,
                            sizeof(detected.default_render_node),
                            render_node);
            }

            if (has_h264)
                detected.has_h264_vaapi = 1;
            if (has_hevc)
                detected.has_hevc_vaapi = 1;

            free(profile_text);
        }
        closedir(dir);
    }

    /* NVENC — NVIDIA (no device path required) */
    detected.has_h264_nvenc = runtime_catalog_component_enabled(catalog_path, "linux", "nvenc", "h264", "h264_nvenc") &&
                              probe_simple_encoder(detected.ffmpeg_bin, "h264_nvenc");
    detected.has_hevc_nvenc = runtime_catalog_component_enabled(catalog_path, "linux", "nvenc", "hevc", "hevc_nvenc") &&
                              probe_simple_encoder(detected.ffmpeg_bin, "hevc_nvenc");
    detected.has_av1_nvenc = runtime_catalog_component_enabled(catalog_path, "linux", "nvenc", "av1", "av1_nvenc") &&
                             ffmpeg_has_encoder(detected.ffmpeg_bin, "av1_nvenc") &&
                             probe_simple_encoder(detected.ffmpeg_bin, "av1_nvenc");
    detected.has_hevc_nvenc_10bit = runtime_catalog_component_enabled(catalog_path, "linux", "nvenc", "hevc_10bit", "hevc_nvenc") &&
                                    probe_simple_encoder_format(detected.ffmpeg_bin, "hevc_nvenc", "p010le", "-profile:v main10 ");
    detected.has_av1_nvenc_10bit = runtime_catalog_component_enabled(catalog_path, "linux", "nvenc", "av1_10bit", "av1_nvenc") &&
                                   probe_simple_encoder_format(detected.ffmpeg_bin, "av1_nvenc", "p010le", "");

    /* AMF — AMD (no device path required) */
    detected.has_h264_amf = runtime_catalog_component_enabled(catalog_path, "linux", "amf", "h264", "h264_amf") &&
                            probe_simple_encoder(detected.ffmpeg_bin, "h264_amf");
    detected.has_hevc_amf = runtime_catalog_component_enabled(catalog_path, "linux", "amf", "hevc", "hevc_amf") &&
                            probe_simple_encoder(detected.ffmpeg_bin, "hevc_amf");
    /* av1_amf requires RDNA3+ (RX 7000 series); pre-filter on -encoders text
     * scan first, since older GPUs will fail the one-frame probe anyway. */
    detected.has_av1_amf = runtime_catalog_component_enabled(catalog_path, "linux", "amf", "av1", "av1_amf") &&
                           ffmpeg_has_encoder(detected.ffmpeg_bin, "av1_amf") &&
                           probe_simple_encoder(detected.ffmpeg_bin, "av1_amf");

    /* QSV — Intel (no device path required) */
    detected.has_h264_qsv = runtime_catalog_component_enabled(catalog_path, "linux", "qsv", "h264", "h264_qsv") &&
                            probe_simple_encoder(detected.ffmpeg_bin, "h264_qsv");
    detected.has_hevc_qsv = runtime_catalog_component_enabled(catalog_path, "linux", "qsv", "hevc", "hevc_qsv") &&
                            probe_simple_encoder(detected.ffmpeg_bin, "hevc_qsv");
    detected.has_av1_qsv = qsv_av1_enabled &&
                           probe_simple_encoder(detected.ffmpeg_bin, "av1_qsv");
    detected.has_hevc_qsv_10bit = qsv_hevc_10bit_enabled &&
                                  probe_simple_encoder_format(detected.ffmpeg_bin, "hevc_qsv",
                                                              "p010le", "-profile:v main10 ");
    detected.has_av1_qsv_10bit = qsv_av1_10bit_enabled &&
                                 probe_simple_encoder_format(detected.ffmpeg_bin, "av1_qsv",
                                                             "p010le", "");

    /* Vulkan — any GPU with Vulkan 1.1+ (compute-shader ProRes) */
    {
        int mask = 0, count = 0;
        int vulkan_prores_enabled = runtime_catalog_component_enabled(catalog_path, "linux", "vulkan", "prores_ks", "prores_ks_vulkan");
        int best = vulkan_prores_enabled ? probe_vulkan_prores(detected.ffmpeg_bin, &mask, &count) : -1;
        detected.has_prores_ks_vulkan = (best >= 0) ? 1 : 0;
        detected.vulkan_working_mask  = mask;
        detected.vulkan_device_index  = (best >= 0) ? best : 0;
        detected.vulkan_device_count  = count;
    }

    /* Vulkan hardware video encoders — h264_vulkan/hevc_vulkan (RDNA3+,
     * Turing+) and av1_vulkan (RDNA3+ / Turing+, ffmpeg >= 8.0). Each is
     * pre-filtered against `ffmpeg -encoders` before the one-frame probe
     * to keep startup time flat on systems without the hardware. */
    {
        int mask = 0, count = 0, best = -1;

        if (runtime_catalog_component_enabled(catalog_path, "linux", "vulkan", "h264", "h264_vulkan") &&
            ffmpeg_has_encoder(detected.ffmpeg_bin, "h264_vulkan"))
            best = probe_vulkan_encoder(detected.ffmpeg_bin, "h264_vulkan", &mask, &count);
        detected.has_h264_vulkan = (best >= 0) ? 1 : 0;
        if (best >= 0) {
            detected.vulkan_hw_working_mask = mask;
            detected.vulkan_hw_device_index = best;
            detected.vulkan_hw_device_count = count;
        }

        if (runtime_catalog_component_enabled(catalog_path, "linux", "vulkan", "hevc", "hevc_vulkan") &&
            ffmpeg_has_encoder(detected.ffmpeg_bin, "hevc_vulkan"))
            best = probe_vulkan_encoder(detected.ffmpeg_bin, "hevc_vulkan", &mask, &count);
        else
            best = -1;
        detected.has_hevc_vulkan = (best >= 0) ? 1 : 0;
        if (best >= 0 && count > detected.vulkan_hw_device_count) {
            detected.vulkan_hw_working_mask = mask;
            detected.vulkan_hw_device_index = best;
            detected.vulkan_hw_device_count = count;
        }

        if (runtime_catalog_component_enabled(catalog_path, "linux", "vulkan", "av1", "av1_vulkan") &&
            ffmpeg_has_encoder(detected.ffmpeg_bin, "av1_vulkan"))
            best = probe_vulkan_encoder(detected.ffmpeg_bin, "av1_vulkan", &mask, &count);
        else
            best = -1;
        detected.has_av1_vulkan = (best >= 0) ? 1 : 0;
        if (best >= 0 && count > detected.vulkan_hw_device_count) {
            detected.vulkan_hw_working_mask = mask;
            detected.vulkan_hw_device_index = best;
            detected.vulkan_hw_device_count = count;
        }
    }

    g_cache.support = detected;
    g_cache.initialized = 1;

    {
        char cache_path[PATH_MAX];
        char signature[768];

        compute_probe_signature(signature, sizeof(signature),
                                detected.ffmpeg_bin, catalog_path);
        if (get_cache_file_path(cache_path, sizeof(cache_path)))
            save_probe_cache(cache_path, signature, &detected);
    }

    if (out_support)
        *out_support = detected;

    return 1;
}

void linux_invalidate_codec_support_cache(void)
{
    char cache_path[PATH_MAX];

    g_cache.initialized = 0;
    memset(&g_cache.support, 0, sizeof(g_cache.support));

    if (get_cache_file_path(cache_path, sizeof(cache_path)))
        unlink(cache_path);
}

int linux_is_bundled_ffmpeg_available(void)
{
    char path[PATH_MAX];

    return resolve_bundled_binary("ffmpeg", path, sizeof(path));
}

int linux_is_bundled_ffprobe_available(void)
{
    char path[PATH_MAX];

    return resolve_bundled_binary("ffprobe", path, sizeof(path));
}

int linux_is_bundled_mkvmerge_available(void)
{
    char path[PATH_MAX];

    return resolve_bundled_binary("mkvmerge", path, sizeof(path));
}

int linux_is_bundled_mp4box_available(void)
{
    char path[PATH_MAX];

    return resolve_bundled_binary("MP4Box", path, sizeof(path));
}

const char *linux_get_preferred_ffmpeg_bin(void)
{
    linux_probe_codec_support(NULL);
    return g_cache.support.ffmpeg_bin;
}

const char *linux_get_preferred_ffprobe_bin(void)
{
    linux_probe_codec_support(NULL);
    return g_cache.support.ffprobe_bin;
}

const char *linux_get_preferred_mkvmerge_bin(void)
{
    linux_probe_codec_support(NULL);
    return g_cache.support.mkvmerge_bin;
}

const char *linux_get_preferred_mp4box_bin(void)
{
    linux_probe_codec_support(NULL);
    return g_cache.support.mp4box_bin;
}

/**
 * Get a friendly name for a VAAPI render device.
 * Attempts to read device name from sysfs, or extracts device node name.
 * Examples:
 *   Input:  "/dev/dri/renderD128"
 *   Output: "Intel UHD Graphics 630" (if found in sysfs)
 *   Fallback: "GPU 0 (renderD128)"
 */
int linux_get_vaapi_device_name(const char *device_path, char *out_name, size_t out_sz)
{
    char sysfs_path[PATH_MAX];
    char device_name[256];
    FILE *fp;
    int device_num = 0;
    const char *node_name;

    if (!device_path || !out_name || out_sz == 0) {
        if (out_name && out_sz > 0)
            out_name[0] = '\0';
        return -1;
    }

    /* Extract device node name (e.g., "renderD128" from "/dev/dri/renderD128") */
    node_name = strrchr(device_path, '/');
    if (node_name)
        node_name++;
    else
        node_name = device_path;

    /* Try to extract device number from node name (renderD128 -> 128) */
    if (sscanf(node_name, "renderD%d", &device_num) != 1)
        device_num = 0;

    /* Try to read device name from sysfs */
    /* Path like: /sys/class/drm/renderD128/name */
    snprintf(sysfs_path, sizeof(sysfs_path),
             "/sys/class/drm/renderD%d/name", device_num);

    fp = fopen(sysfs_path, "r");
    if (fp) {
        if (fgets(device_name, sizeof(device_name), fp) != NULL) {
            /* Remove trailing newline */
            size_t len = strlen(device_name);
            if (len > 0 && device_name[len - 1] == '\n')
                device_name[len - 1] = '\0';

            /* Use the sysfs name if we got it */
            snprintf(out_name, out_sz, "%s (%s)", device_name, node_name);
            fclose(fp);
            return 0;
        }
        fclose(fp);
    }

    /* Fallback: derive the label from the stable render-node number. */
    snprintf(out_name, out_sz, "GPU %d (%s)", device_num, node_name);
    return 0;
}
