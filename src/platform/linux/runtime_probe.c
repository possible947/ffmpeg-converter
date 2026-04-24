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
    char process_dir[PATH_MAX];

    if (!name || !out_path || out_path_sz == 0)
        return 0;

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

static int probe_vaapi_encoder(const char *ffmpeg_bin,
                               const char *render_node,
                               const char *encoder_name)
{
    char cmd[8192];
    int rc;

    if (!ffmpeg_bin || !render_node || !encoder_name)
        return 0;

    snprintf(cmd,
             sizeof(cmd),
             "\"%s\" -v error -hide_banner "
             "-init_hw_device vaapi=va:\"%s\" "
             "-f lavfi -i color=size=1920x1080:rate=1 "
             "-frames:v 1 -vf format=nv12,hwupload "
             "-c:v %s -f null - >/dev/null 2>&1",
             ffmpeg_bin,
             render_node,
             encoder_name);

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