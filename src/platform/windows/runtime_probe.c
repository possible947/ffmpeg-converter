/**
 * runtime_probe.c (Windows)
 * Windows-specific runtime probing: binary resolution and GPU encoder detection.
 * Detects NVIDIA NVENC, AMD AMF, and Intel QSV via short test encodes.
 */

#include "runtime_probe.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global cache — filled once on first call to windows_probe_codec_support() */
static struct {
    int              probed;
    WindowsCodecSupport support;
} g_cache;

/* ---- Platform-specific helpers ---------------------------------------- */

/**
 * windows_get_process_dir()
 * Uses GetModuleFileNameW() to get the executable path as UTF-16.
 * Converts to UTF-8, then strips the filename to obtain the directory.
 * Returns 1 on success, 0 on failure.
 */
static int windows_get_process_dir(char *out_dir, size_t out_dir_sz)
{
    wchar_t wide_path[MAX_PATH];
    char    utf8_path[MAX_PATH * 4];
    char   *last_sep;
    DWORD   len;

    if (!out_dir || out_dir_sz == 0)
        return 0;

    len = GetModuleFileNameW(NULL, wide_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return 0;

    if (!WideCharToMultiByte(CP_UTF8, 0, wide_path, -1,
                             utf8_path, (int)sizeof(utf8_path), NULL, NULL))
        return 0;

    /* Find last backslash or forward slash */
    last_sep = strrchr(utf8_path, '\\');
    if (!last_sep)
        last_sep = strrchr(utf8_path, '/');
    if (!last_sep)
        return 0;

    *last_sep = '\0';
    copy_string(out_dir, out_dir_sz, utf8_path);
    return 1;
}

/**
 * windows_is_executable_file()
 * On Windows, "executable" means:
 *   1. File exists and is not a directory.
 *   2. Has a .exe, .bat, or .cmd extension (case-insensitive).
 */
static int windows_is_executable_file(const char *path)
{
    DWORD  attrs;
    size_t len;

    if (!path || path[0] == '\0')
        return 0;

    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return 0;

    len = strlen(path);
    if (len >= 4) {
        const char *ext = path + len - 4;
        if (_stricmp(ext, ".exe") == 0 ||
            _stricmp(ext, ".bat") == 0 ||
            _stricmp(ext, ".cmd") == 0)
            return 1;
    }
    return 0;
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
    char candidate[MAX_PATH];

    if (!base_dir || base_dir[0] == '\0' || !relative_path || !name)
        return 0;

    if (relative_path[0] != '\0')
        snprintf(candidate, sizeof(candidate), "%s\\%s\\%s",
                 base_dir, relative_path, name);
    else
        snprintf(candidate, sizeof(candidate), "%s\\%s", base_dir, name);

    if (!windows_is_executable_file(candidate))
        return 0;

    copy_string(out_path, out_path_sz, candidate);
    return 1;
}

/**
 * windows_resolve_path_binary()
 * Search PATH (semicolon-separated) for an executable named <name>.exe.
 * Returns 1 and fills out_path on success.
 */
static int windows_resolve_path_binary(const char *name,
                                       char *out_path,
                                       size_t out_path_sz)
{
    const char *path_env;
    char        path_copy[8192];
    char        name_exe[MAX_PATH];
    char       *dir;
    char       *saveptr = NULL;
    size_t      name_len;

    if (!name || !out_path || out_path_sz == 0)
        return 0;

    /* Ensure .exe suffix */
    copy_string(name_exe, sizeof(name_exe), name);
    name_len = strlen(name_exe);
    if (name_len < 4 ||
        _stricmp(name_exe + name_len - 4, ".exe") != 0) {
        strncat(name_exe, ".exe", sizeof(name_exe) - name_len - 1);
    }

    path_env = getenv("PATH");
    if (!path_env || path_env[0] == '\0')
        return 0;

    copy_string(path_copy, sizeof(path_copy), path_env);
    dir = strtok_s(path_copy, ";", &saveptr);
    while (dir) {
        char candidate[MAX_PATH];

        if (dir[0] != '\0') {
            snprintf(candidate, sizeof(candidate), "%s\\%s", dir, name_exe);
            if (windows_is_executable_file(candidate)) {
                copy_string(out_path, out_path_sz, candidate);
                return 1;
            }
        }
        dir = strtok_s(NULL, ";", &saveptr);
    }
    return 0;
}

/**
 * windows_resolve_bundled_binary()
 * Search order:
 *   1. <exe_dir>\<name>.exe
 *   2. <exe_dir>\bin\<name>.exe
 *   3. FFMPEG_CONVERTER_SOURCE_DIR\src\platform\windows\bin\<name>.exe (dev builds)
 * Returns 1 and fills out_path on success.
 */
static int windows_resolve_bundled_binary(const char *name,
                                          char *out_path,
                                          size_t out_path_sz)
{
    char exe_dir[MAX_PATH];
    char name_exe[MAX_PATH];
    size_t name_len;

    if (!name || !out_path || out_path_sz == 0)
        return 0;

    /* Ensure .exe suffix for the bundled name */
    copy_string(name_exe, sizeof(name_exe), name);
    name_len = strlen(name_exe);
    if (name_len < 4 ||
        _stricmp(name_exe + name_len - 4, ".exe") != 0) {
        strncat(name_exe, ".exe", sizeof(name_exe) - name_len - 1);
    }

    if (!windows_get_process_dir(exe_dir, sizeof(exe_dir)))
        return 0;

    if (try_bundled_candidate(exe_dir, "", name_exe, out_path, out_path_sz))
        return 1;

    if (try_bundled_candidate(exe_dir, "bin", name_exe, out_path, out_path_sz))
        return 1;

#ifdef FFMPEG_CONVERTER_SOURCE_DIR
    {
        char dev_bin_dir[MAX_PATH];
        snprintf(dev_bin_dir, sizeof(dev_bin_dir),
                 FFMPEG_CONVERTER_SOURCE_DIR "\\src\\platform\\windows\\bin");
        if (try_bundled_candidate(dev_bin_dir, "", name_exe, out_path, out_path_sz))
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
    if (env_path && windows_is_executable_file(env_path)) {
        copy_string(out_path, out_path_sz, env_path);
        return;
    }

    env_path = env_name_secondary ? getenv(env_name_secondary) : NULL;
    if (env_path && windows_is_executable_file(env_path)) {
        copy_string(out_path, out_path_sz, env_path);
        return;
    }

    if (windows_resolve_bundled_binary(binary_name, out_path, out_path_sz)) {
        if (using_bundled)
            *using_bundled = 1;
        return;
    }

    if (windows_resolve_path_binary(binary_name, out_path, out_path_sz))
        return;

    /* Fall back to bare name and hope it is on PATH at invocation time */
    copy_string(out_path, out_path_sz, binary_name);
}

/* ---- GPU Encoder Detection -------------------------------------------- */

/**
 * windows_path_is_cmd_safe()
 * Returns 1 if the path contains no characters that would be interpreted
 * by cmd.exe inside a double-quoted argument (", %, &, |, <, >, ^).
 * Paths failing this check are not used in system() calls.
 */
static int windows_path_is_cmd_safe(const char *path)
{
    const char *p;
    if (!path)
        return 0;
    for (p = path; *p != '\0'; ++p) {
        if (*p == '"' || *p == '%' || *p == '&' ||
            *p == '|' || *p == '<' || *p == '>' || *p == '^')
            return 0;
    }
    return 1;
}

/**
 * windows_probe_encoder()
 * Test a single GPU encoder via a one-frame ffmpeg encode.
 * Returns 1 if the encoder is available, 0 otherwise.
 */
static int windows_probe_encoder(const char *ffmpeg_bin,
                                 const char *encoder_name)
{
    char cmd[8192];
    int  rc;

    if (!ffmpeg_bin || ffmpeg_bin[0] == '\0' || !encoder_name)
        return 0;
    if (!windows_path_is_cmd_safe(ffmpeg_bin))
        return 0;

    snprintf(cmd, sizeof(cmd),
             "\"%s\" -v error -hide_banner "
             "-f lavfi -i color=size=1920x1080:rate=1 "
             "-frames:v 1 "
             "-c:v %s -f null - 2>nul",
             ffmpeg_bin, encoder_name);

    rc = system(cmd);
    return rc == 0;
}

/**
 * windows_probe_vulkan_prores()
 * Tests prores_ks_vulkan on Vulkan device vk:1 (hard-locked).
 * vk:0 is Intel Arc on this system which produces broken output;
 * vk:1 is NVIDIA and produces correct ProRes output.
 * Returns 1 if the encode exits 0, -1 if unavailable.
 */
static int windows_probe_vulkan_prores(const char *ffmpeg_bin)
{
    char cmd[8192];

    if (!ffmpeg_bin || ffmpeg_bin[0] == '\0') return -1;
    if (!windows_path_is_cmd_safe(ffmpeg_bin)) return -1;

    snprintf(cmd, sizeof(cmd),
             "\"%s\" -v error -hide_banner "
             "-init_hw_device vulkan=vk:1 -filter_hw_device vk "
             "-f lavfi -i color=size=1920x1080:rate=1 "
             "-frames:v 1 "
             "-vf format=yuv422p10le,hwupload "
             "-c:v prores_ks_vulkan -f null - 2>nul",
             ffmpeg_bin);

    return system(cmd) == 0 ? 1 : -1;
}

/* ---- Main Probe Function ----------------------------------------------- */

/**
 * windows_probe_codec_support()
 * Resolves binary paths and probes all GPU encoders.
 * Results are cached after the first call.
 * Returns 1 on success.
 */
int windows_probe_codec_support(WindowsCodecSupport *out_support)
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

    /* Probe NVENC encoders */
    g_cache.support.has_h264_nvenc =
        windows_probe_encoder(g_cache.support.bins.ffmpeg_bin, "h264_nvenc");
    g_cache.support.has_hevc_nvenc =
        windows_probe_encoder(g_cache.support.bins.ffmpeg_bin, "hevc_nvenc");

    /* Probe AMD AMF encoders */
    g_cache.support.has_h264_amf =
        windows_probe_encoder(g_cache.support.bins.ffmpeg_bin, "h264_amf");
    g_cache.support.has_hevc_amf =
        windows_probe_encoder(g_cache.support.bins.ffmpeg_bin, "hevc_amf");

    /* Probe Intel QSV encoders */
    g_cache.support.has_h264_qsv =
        windows_probe_encoder(g_cache.support.bins.ffmpeg_bin, "h264_qsv");
    g_cache.support.has_hevc_qsv =
        windows_probe_encoder(g_cache.support.bins.ffmpeg_bin, "hevc_qsv");

    /* Probe Vulkan ProRes encoder (requires full Vulkan device init pipeline) */
    {
        int vk_idx = windows_probe_vulkan_prores(g_cache.support.bins.ffmpeg_bin);
        g_cache.support.has_prores_ks_vulkan  = (vk_idx >= 0) ? 1 : 0;
        g_cache.support.vulkan_device_index   = (vk_idx >= 0) ? vk_idx : 0;
    }

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

int windows_is_bundled_ffmpeg_available(void)
{
    char path[MAX_PATH];
    return windows_resolve_bundled_binary("ffmpeg", path, sizeof(path));
}

int windows_is_bundled_ffprobe_available(void)
{
    char path[MAX_PATH];
    return windows_resolve_bundled_binary("ffprobe", path, sizeof(path));
}

int windows_is_bundled_mkvmerge_available(void)
{
    char path[MAX_PATH];
    return windows_resolve_bundled_binary("mkvmerge", path, sizeof(path));
}

int windows_is_bundled_mp4box_available(void)
{
    char path[MAX_PATH];
    return windows_resolve_bundled_binary("MP4Box", path, sizeof(path));
}

const char *windows_get_preferred_ffmpeg_bin(void)
{
    windows_probe_codec_support(NULL);
    return g_cache.support.bins.ffmpeg_bin;
}

const char *windows_get_preferred_ffprobe_bin(void)
{
    windows_probe_codec_support(NULL);
    return g_cache.support.bins.ffprobe_bin;
}

const char *windows_get_preferred_mkvmerge_bin(void)
{
    windows_probe_codec_support(NULL);
    return g_cache.support.bins.mkvmerge_bin;
}

const char *windows_get_preferred_mp4box_bin(void)
{
    windows_probe_codec_support(NULL);
    return g_cache.support.bins.mp4box_bin;
}
