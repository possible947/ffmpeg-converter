/**
 * runtime_probe.c (macOS)
 * macOS-specific runtime probing: binary resolution and GPU encoder detection.
 * Detects Apple VideoToolbox encoders via short test encodes.
 */

#include "runtime_probe.h"

#include <mach-o/dyld.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Global cache — filled once on first call to macos_probe_codec_support() */
static struct {
    int           probed;
    MacosCodecSupport support;
} g_cache;

/* ---- Platform-specific helpers ---------------------------------------- */

/**
 * macos_is_executable_file()
 * Returns 1 if path exists and is executable by the current process.
 */
static int macos_is_executable_file(const char *path)
{
    return path && path[0] != '\0' && access(path, X_OK) == 0;
}

/**
 * macos_get_process_dir()
 * Uses _NSGetExecutablePath() to get the executable path.
 * Resolves symlinks via realpath() (important for Homebrew-installed binaries).
 * Strips the filename to obtain the directory.
 * Returns 1 on success, 0 on failure.
 */
static int macos_get_process_dir(char *out_dir, size_t out_dir_sz)
{
    char     exe_path[PATH_MAX];
    char     real_path[PATH_MAX];
    uint32_t size = (uint32_t)sizeof(exe_path);
    char    *last_slash;

    if (!out_dir || out_dir_sz == 0)
        return 0;

    if (_NSGetExecutablePath(exe_path, &size) != 0)
        return 0;

    /* Resolve symlinks (critical for Homebrew binaries) */
    if (!realpath(exe_path, real_path))
        copy_string(real_path, sizeof(real_path), exe_path);

    last_slash = strrchr(real_path, '/');
    if (!last_slash)
        return 0;

    *last_slash = '\0';
    copy_string(out_dir, out_dir_sz, real_path);
    return 1;
}

/**
 * try_bundled_candidate()
 * Build a candidate path from base_dir + relative_path + name and check
 * whether it is executable.  Returns 1 and fills out_path on success.
 */
static int try_bundled_candidate(const char *base_dir,
                                 const char *relative_path,
                                 const char *name,
                                 char *out_path,
                                 size_t out_path_sz)
{
    char candidate[PATH_MAX];

    if (!base_dir || base_dir[0] == '\0' || !relative_path || !name)
        return 0;

    if (relative_path[0] != '\0')
        snprintf(candidate, sizeof(candidate), "%s/%s/%s",
                 base_dir, relative_path, name);
    else
        snprintf(candidate, sizeof(candidate), "%s/%s", base_dir, name);

    if (!macos_is_executable_file(candidate))
        return 0;

    copy_string(out_path, out_path_sz, candidate);
    return 1;
}

/**
 * macos_resolve_path_binary()
 * Iterate over PATH (colon-separated) and check each directory for <name>.
 * Returns 1 and fills out_path on success.
 */
static int macos_resolve_path_binary(const char *name,
                                     char *out_path,
                                     size_t out_path_sz)
{
    const char *path_env;
    char        path_copy[8192];
    char       *dir;
    char       *saveptr = NULL;

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
            if (macos_is_executable_file(candidate)) {
                copy_string(out_path, out_path_sz, candidate);
                return 1;
            }
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }
    return 0;
}

/**
 * macos_resolve_bundled_binary()
 * Search order:
 *   1. <exe_dir>/<name>
 *   2. <exe_dir>/bin/<name>
 *   3. FFMPEG_CONVERTER_SOURCE_DIR/src/platform/macos/bin/<name> (dev builds)
 * Returns 1 and fills out_path on success.
 */
static int macos_resolve_bundled_binary(const char *name,
                                        char *out_path,
                                        size_t out_path_sz)
{
    char exe_dir[PATH_MAX];

    if (!name || !out_path || out_path_sz == 0)
        return 0;

    if (!macos_get_process_dir(exe_dir, sizeof(exe_dir)))
        return 0;

    if (try_bundled_candidate(exe_dir, "", name, out_path, out_path_sz))
        return 1;

    if (try_bundled_candidate(exe_dir, "bin", name, out_path, out_path_sz))
        return 1;

#ifdef FFMPEG_CONVERTER_SOURCE_DIR
    {
        char dev_bin_dir[PATH_MAX];
        snprintf(dev_bin_dir, sizeof(dev_bin_dir),
                 FFMPEG_CONVERTER_SOURCE_DIR "/src/platform/macos/bin");
        if (try_bundled_candidate(dev_bin_dir, "", name, out_path, out_path_sz))
            return 1;
    }
#endif

    return 0;
}

/**
 * resolve_preferred_binary()
 * Resolution order:
 *   1. Environment variable env_name_primary (if set and executable)
 *   2. Environment variable env_name_secondary (if set and executable)
 *   3. Bundled copy next to the executable
 *   4. System PATH
 *   5. Fall back to bare binary name (let the shell resolve it)
 */
static void resolve_preferred_binary(const char *env_name_primary,
                                     const char *env_name_secondary,
                                     const char *binary_name,
                                     char *out_path,
                                     size_t out_path_sz,
                                     int *using_bundled)
{
    const char *env_path;

    if (using_bundled)
        *using_bundled = 0;

    env_path = env_name_primary ? getenv(env_name_primary) : NULL;
    if (env_path && macos_is_executable_file(env_path)) {
        copy_string(out_path, out_path_sz, env_path);
        return;
    }

    env_path = env_name_secondary ? getenv(env_name_secondary) : NULL;
    if (env_path && macos_is_executable_file(env_path)) {
        copy_string(out_path, out_path_sz, env_path);
        return;
    }

    if (macos_resolve_bundled_binary(binary_name, out_path, out_path_sz)) {
        if (using_bundled)
            *using_bundled = 1;
        return;
    }

    if (macos_resolve_path_binary(binary_name, out_path, out_path_sz))
        return;

    /* Fall back to bare name and hope it is on PATH at invocation time */
    copy_string(out_path, out_path_sz, binary_name);
}

/* ---- GPU Encoder Detection -------------------------------------------- */

/**
 * macos_path_is_shell_safe()
 * Returns 1 if the path contains no characters that would be interpreted
 * by the shell inside a double-quoted argument (", $, `, \).
 * Paths failing this check are not used in system() calls.
 */
static int macos_path_is_shell_safe(const char *path)
{
    const char *p;
    if (!path)
        return 0;
    for (p = path; *p != '\0'; ++p) {
        if (*p == '"' || *p == '$' || *p == '`' || *p == '\\')
            return 0;
    }
    return 1;
}

/**
 * macos_probe_vt_encoder()
 * Test a VideoToolbox encoder via a one-frame ffmpeg encode.
 * Uses POSIX redirect (>/dev/null 2>&1).
 * Returns 1 if the encoder is available, 0 otherwise.
 */
static int macos_probe_vt_encoder(const char *ffmpeg_bin,
                                  const char *encoder_name)
{
    char cmd[8192];
    int  rc;

    if (!ffmpeg_bin || ffmpeg_bin[0] == '\0' || !encoder_name)
        return 0;
    if (!macos_path_is_shell_safe(ffmpeg_bin))
        return 0;

    snprintf(cmd, sizeof(cmd),
             "\"%s\" -v error -hide_banner "
             "-f lavfi -i color=size=1920x1080:rate=1 "
             "-frames:v 1 "
             "-c:v %s -f null - >/dev/null 2>&1",
             ffmpeg_bin, encoder_name);

    rc = system(cmd);
    if (rc == -1)
        return 0;

    return WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
}

/* ---- Main Probe Function ----------------------------------------------- */

/**
 * macos_probe_codec_support()
 * Resolves binary paths and probes VideoToolbox encoders.
 * Results are cached after the first call.
 * Returns 1 on success.
 */
int macos_probe_codec_support(MacosCodecSupport *out_support)
{
    if (g_cache.probed) {
        if (out_support)
            *out_support = g_cache.support;
        return 1;
    }

    /* Resolve ffmpeg first — needed for encoder probing */
    resolve_preferred_binary("FFMPEG", "FFMPEG_BIN", "ffmpeg",
                             g_cache.support.bins.ffmpeg_bin,
                             sizeof(g_cache.support.bins.ffmpeg_bin),
                             &g_cache.support.bins.using_bundled_ffmpeg);

    /* Probe VideoToolbox encoders */
    g_cache.support.has_h264_videotoolbox =
        macos_probe_vt_encoder(g_cache.support.bins.ffmpeg_bin,
                               "h264_videotoolbox");
    g_cache.support.has_hevc_videotoolbox =
        macos_probe_vt_encoder(g_cache.support.bins.ffmpeg_bin,
                               "hevc_videotoolbox");

    /* Resolve remaining tool binaries */
    resolve_preferred_binary("FFPROBE", "FFPROBE_BIN", "ffprobe",
                             g_cache.support.bins.ffprobe_bin,
                             sizeof(g_cache.support.bins.ffprobe_bin),
                             &g_cache.support.bins.using_bundled_ffprobe);

    resolve_preferred_binary("MKVMERGE", "MKVMERGE_BIN", "mkvmerge",
                             g_cache.support.bins.mkvmerge_bin,
                             sizeof(g_cache.support.bins.mkvmerge_bin),
                             &g_cache.support.bins.using_bundled_mkvmerge);

    resolve_preferred_binary("MP4BOX", "MP4BOX_BIN", "MP4Box",
                             g_cache.support.bins.mp4box_bin,
                             sizeof(g_cache.support.bins.mp4box_bin),
                             &g_cache.support.bins.using_bundled_mp4box);

    g_cache.probed = 1;
    if (out_support)
        *out_support = g_cache.support;
    return 1;
}

/* ---- Public API -------------------------------------------------------- */

int macos_is_bundled_ffmpeg_available(void)
{
    char path[PATH_MAX];
    return macos_resolve_bundled_binary("ffmpeg", path, sizeof(path));
}

int macos_is_bundled_ffprobe_available(void)
{
    char path[PATH_MAX];
    return macos_resolve_bundled_binary("ffprobe", path, sizeof(path));
}

int macos_is_bundled_mkvmerge_available(void)
{
    char path[PATH_MAX];
    return macos_resolve_bundled_binary("mkvmerge", path, sizeof(path));
}

int macos_is_bundled_mp4box_available(void)
{
    char path[PATH_MAX];
    return macos_resolve_bundled_binary("MP4Box", path, sizeof(path));
}

const char *macos_get_preferred_ffmpeg_bin(void)
{
    macos_probe_codec_support(NULL);
    return g_cache.support.bins.ffmpeg_bin;
}

const char *macos_get_preferred_ffprobe_bin(void)
{
    macos_probe_codec_support(NULL);
    return g_cache.support.bins.ffprobe_bin;
}

const char *macos_get_preferred_mkvmerge_bin(void)
{
    macos_probe_codec_support(NULL);
    return g_cache.support.bins.mkvmerge_bin;
}

const char *macos_get_preferred_mp4box_bin(void)
{
    macos_probe_codec_support(NULL);
    return g_cache.support.bins.mp4box_bin;
}
