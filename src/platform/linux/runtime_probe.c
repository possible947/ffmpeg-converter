#include "runtime_probe.h"
#include "../runtime_probe_common.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    int initialized;
    LinuxCodecSupport support;
} LinuxCodecSupportCache;

static LinuxCodecSupportCache g_cache;

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
    char *q;
    int  rc;

    if (!ffmpeg_bin || ffmpeg_bin[0] == '\0' || !encoder_name)
        return 0;

    q = posix_shell_quote(ffmpeg_bin);
    if (!q) return 0;

    snprintf(cmd, sizeof(cmd),
             "%s -v error -hide_banner "
             "-f lavfi -i color=size=1920x1080:rate=1 "
             "-frames:v 1 "
             "-c:v %s -f null - >/dev/null 2>&1",
             q, encoder_name);
    free(q);

    rc = system(cmd);
    if (rc == -1)
        return 0;

    return WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
}

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
        int  rc;

        snprintf(cmd, sizeof(cmd),
                 "%s -v error -hide_banner "
                 "-init_hw_device vulkan=vk:%d -filter_hw_device vk "
                 "-f lavfi -i color=size=1920x1080:rate=1 "
                 "-frames:v 1 "
                 "-vf format=yuv422p10le,hwupload "
                 "-c:v prores_ks_vulkan -f null - >/dev/null 2>&1",
                 q, i);

        rc = system(cmd);
        if (rc == -1)
            break;  /* system() failure — stop scanning */

        if (WIFEXITED(rc) && WEXITSTATUS(rc) == 0) {
            mask |= (1 << i);
            best = i;
            count++;
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
        int  rc;

        if (vulkan_device_is_software(i))
            continue;

        snprintf(cmd, sizeof(cmd),
                 "%s -v error -hide_banner "
                 "-init_hw_device vulkan=vk:%d -filter_hw_device vk "
                 "-f lavfi -i color=size=1920x1080:rate=1 "
                 "-frames:v 1 "
                 "-vf format=nv12,hwupload "
                 "-c:v %s -f null - >/dev/null 2>&1",
                 q, i, encoder_name);

        rc = system(cmd);
        if (rc == -1)
            break;  /* system() failure — stop scanning */

        if (WIFEXITED(rc) && WEXITSTATUS(rc) == 0) {
            mask |= (1 << i);
            best = i;
            count++;
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

static int probe_vaapi_encoder(const char *ffmpeg_bin,
                               const char *render_node,
                               const char *encoder_name)
{
    char cmd[8192];
    char *q;
    char *q_node;
    int rc;

    if (!ffmpeg_bin || !render_node || !encoder_name)
        return 0;

    q = posix_shell_quote(ffmpeg_bin);
    if (!q) return 0;

    q_node = posix_shell_quote(render_node);
    if (!q_node) { free(q); return 0; }

    snprintf(cmd,
             sizeof(cmd),
             "%s -v error -hide_banner "
             "-init_hw_device vaapi=va:%s "
             "-f lavfi -i color=size=1920x1080:rate=1 "
             "-frames:v 1 -vf format=nv12,hwupload "
             "-c:v %s -f null - >/dev/null 2>&1",
             q,
             q_node,
             encoder_name);
    free(q);
    free(q_node);

    rc = system(cmd);
    if (rc == -1)
        return 0;

    return WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
}

int linux_probe_codec_support(LinuxCodecSupport *out_support)
{
    LinuxCodecSupport detected;
    DIR *dir;
    struct dirent *entry;

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

    dir = opendir("/dev/dri");
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            char render_node[PATH_MAX];
            int has_h264;
            int has_hevc;

            if (!starts_with(entry->d_name, "renderD"))
                continue;

            snprintf(render_node, sizeof(render_node), "/dev/dri/%s", entry->d_name);
            if (access(render_node, R_OK | W_OK) != 0)
                continue;

            has_h264 = probe_vaapi_encoder(detected.ffmpeg_bin, render_node, "h264_vaapi");
            has_hevc = probe_vaapi_encoder(detected.ffmpeg_bin, render_node, "hevc_vaapi");

            if (!detected.default_render_node[0] && (has_h264 || has_hevc)) {
                copy_string(detected.default_render_node,
                            sizeof(detected.default_render_node),
                            render_node);
            }

            if (has_h264)
                detected.has_h264_vaapi = 1;
            if (has_hevc)
                detected.has_hevc_vaapi = 1;
        }
        closedir(dir);
    }

    /* NVENC — NVIDIA (no device path required) */
    detected.has_h264_nvenc = probe_simple_encoder(detected.ffmpeg_bin, "h264_nvenc");
    detected.has_hevc_nvenc = probe_simple_encoder(detected.ffmpeg_bin, "hevc_nvenc");

    /* AMF — AMD (no device path required) */
    detected.has_h264_amf = probe_simple_encoder(detected.ffmpeg_bin, "h264_amf");
    detected.has_hevc_amf = probe_simple_encoder(detected.ffmpeg_bin, "hevc_amf");
    /* av1_amf requires RDNA3+ (RX 7000 series); pre-filter on -encoders text
     * scan first, since older GPUs will fail the one-frame probe anyway. */
    detected.has_av1_amf = ffmpeg_has_encoder(detected.ffmpeg_bin, "av1_amf") &&
                           probe_simple_encoder(detected.ffmpeg_bin, "av1_amf");

    /* QSV — Intel (no device path required) */
    detected.has_h264_qsv = probe_simple_encoder(detected.ffmpeg_bin, "h264_qsv");
    detected.has_hevc_qsv = probe_simple_encoder(detected.ffmpeg_bin, "hevc_qsv");

    /* Vulkan — any GPU with Vulkan 1.1+ (compute-shader ProRes) */
    {
        int mask = 0, count = 0;
        int best = probe_vulkan_prores(detected.ffmpeg_bin, &mask, &count);
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

        if (ffmpeg_has_encoder(detected.ffmpeg_bin, "h264_vulkan"))
            best = probe_vulkan_encoder(detected.ffmpeg_bin, "h264_vulkan", &mask, &count);
        detected.has_h264_vulkan = (best >= 0) ? 1 : 0;
        if (best >= 0) {
            detected.vulkan_hw_working_mask = mask;
            detected.vulkan_hw_device_index = best;
            detected.vulkan_hw_device_count = count;
        }

        if (ffmpeg_has_encoder(detected.ffmpeg_bin, "hevc_vulkan"))
            best = probe_vulkan_encoder(detected.ffmpeg_bin, "hevc_vulkan", &mask, &count);
        else
            best = -1;
        detected.has_hevc_vulkan = (best >= 0) ? 1 : 0;
        if (best >= 0 && count > detected.vulkan_hw_device_count) {
            detected.vulkan_hw_working_mask = mask;
            detected.vulkan_hw_device_index = best;
            detected.vulkan_hw_device_count = count;
        }

        if (ffmpeg_has_encoder(detected.ffmpeg_bin, "av1_vulkan"))
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

    if (out_support)
        *out_support = detected;

    return 1;
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
    static int device_counter = 0;

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
        device_num = device_counter++;

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

    /* Fallback: use just the device node name with a simple prefix */
    snprintf(out_name, out_sz, "GPU %d (%s)", device_counter++, node_name);
    return 0;
}
