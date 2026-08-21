/** converter_platform.h
 * Platform abstraction interface for converter.c
 * All platform-specific operations are declared here and implemented
 * in platform/converter_{linux,macos,windows}.c
 *
 * Rules:
 *  - No platform #ifdef in this file
 *  - No implementation in this file (header only)
 *  - Every function must be implemented on every supported platform
 */

#ifndef CONVERTER_PLATFORM_H
#define CONVERTER_PLATFORM_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------
 *  Lifecycle
 * --------------------------------------------------------------- */

/**
 * platform_init() — Called once from converter_create().
 * Resolves binary paths, detects GPU capabilities, and caches results.
 * Returns 0 on success, non-zero on error.
 */
int platform_init(void);

/**
 * platform_cleanup() — Called from converter_destroy().
 * Releases any resources allocated by platform_init().
 */
void platform_cleanup(void);

/* ---------------------------------------------------------------
 *  Binary resolution
 * --------------------------------------------------------------- */

/**
 * Returns the path to the ffmpeg binary, or "" if not found.
 * Priority: FFMPEG env → FFMPEG_BIN env → platform search → "".
 * The returned pointer is valid for the lifetime of the process.
 */
const char* platform_get_ffmpeg_bin(void);

/**
 * Returns the path to the ffprobe binary, or "" if not found.
 */
const char* platform_get_ffprobe_bin(void);

/**
 * Returns the path to the mkvmerge binary, or "" if not found.
 */
const char* platform_get_mkvmerge_bin(void);

/**
 * Returns the path to the MP4Box binary, or "" if not found.
 */
const char* platform_get_mp4box_bin(void);

/* ---------------------------------------------------------------
 *  Path operations
 * --------------------------------------------------------------- */

/**
 * Returns a shell-escaped version of path suitable for inclusion in
 * a command string passed to popen().
 * Caller must free the returned string.
 * On POSIX: wraps in single quotes and escapes embedded single quotes.
 * On Windows: wraps in double quotes and escapes embedded double quotes.
 */
char* platform_escape_path_for_command(const char* path);

/**
 * Creates a directory and all intermediate directories.
 * Handles both '/' and '\\' separators.
 * Returns 0 on success, -1 on error (with errno set).
 */
int platform_mkdir_recursive(const char* path);

/**
 * Returns the user's home directory path.
 * Linux/macOS: getenv("HOME")
 * Windows: getenv("USERPROFILE") or HOMEDRIVE+HOMEPATH
 * Never returns NULL; falls back to "." if not found.
 */
const char* platform_get_home_dir(void);

/**
 * Returns the filename component of a path (last component after separator).
 * Does not modify the input.
 * Examples: "/foo/bar.mkv" → "bar.mkv", "C:\\dir\\file.mp4" → "file.mp4"
 */
const char* platform_get_filename(const char* path);

/**
 * Joins a directory path and filename with the platform separator.
 * Caller must free the returned string.
 * Returns NULL on allocation failure.
 */
char* platform_join_paths(const char* dir, const char* file);

/**
 * Returns 1 if path is absolute, 0 if relative.
 * POSIX: starts with '/'
 * Windows: starts with drive letter + ':' or UNC '\\\\'
 */
int platform_path_is_absolute(const char* path);

/**
 * Returns the platform-specific null device string for shell redirection.
 * POSIX: "/dev/null"
 * Windows: "nul"
 */
const char* platform_get_null_device(void);

/**
 * Returns 1 if the file at path is readable by the current process.
 * 0 otherwise.
 */
int platform_is_file_readable(const char* path);

/**
 * Returns 1 if the directory at path is writable by the current process.
 * 0 otherwise.
 */
int platform_is_dir_writable(const char* path);

/* ---------------------------------------------------------------
 *  File-system and process helpers
 * --------------------------------------------------------------- */

/**
 * Returns 1 if path refers to a regular file (exists and is not a directory).
 * POSIX: stat() + S_ISREG
 * Windows: GetFileAttributesA() without FILE_ATTRIBUTE_DIRECTORY
 */
int platform_stat_is_regular_file(const char *path);

/**
 * Returns 1 if path refers to an existing directory.
 * POSIX: stat() + S_ISDIR
 * Windows: GetFileAttributesA() with FILE_ATTRIBUTE_DIRECTORY
 */
int platform_stat_is_directory(const char *path);

/**
 * Open a process pipe for reading (replaces popen()).
 * POSIX: popen(cmd, mode)
 * Windows: _popen(cmd, mode)
 */
FILE *platform_popen(const char *cmd, const char *mode);

/**
 * Close a process pipe and return its raw exit status (replaces pclose()).
 * POSIX: pclose(fp)
 * Windows: _pclose(fp)
 */
int platform_pclose(FILE *fp);

/* ---------------------------------------------------------------
 *  Output handling
 * --------------------------------------------------------------- */

/**
 * Normalizes a line read from popen() output.
 * On Windows: strips trailing '\\r' before '\\n'.
 * On POSIX: no-op.
 * Modifies line in-place.
 */
void platform_normalize_output_line(char* line);

/* ---------------------------------------------------------------
 *  Audio and GPU support
 * --------------------------------------------------------------- */

/**
 * Validates that all required FFmpeg audio filters are available.
 * Checks: aresample (with soxr), volumedetect, loudnorm, volume, asplit.
 * Returns 1 if all required filters are present, 0 otherwise.
 * On failure, caller should report ERR_AUDIO_FILTER_VALIDATION_FAILED.
 */
int platform_validate_audio_filters(void);

/**
 * Returns 1 if the named codec is supported on this platform.
 * Examples: "hevc_vaapi" (Linux), "hevc_videotoolbox" (macOS),
 *           "h264_nvenc" (Windows with NVIDIA GPU).
 * Cross-platform codecs ("copy", "prores", "prores_ks") always return 1.
 */
int platform_supports_codec(const char* codec);

/**
 * Returns the ffmpeg video codec flags string for the given platform-specific
 * codec name (e.g., "-c:v hevc_vaapi -rc_mode auto ").
 * Returns NULL if codec is not a platform-specific codec — the caller
 * should then handle it as a common codec.
 * The returned pointer is valid until the next call from the same thread.
 */
const char* platform_get_video_codec_flags(const char* codec,
                                           const char* input_path,
                                           const void* opts);

/**
 * Detects GPU hardware acceleration support.
 * Returns a bitmask of PLAT_CAP_* flags indicating available capabilities.
 */
int platform_detect_gpu_support(void);

/**
 * Fills hw_device with the default hardware device path for the given codec.
 * Used on Linux for VAAPI render node resolution.
 * Returns 1 if hw_device was filled, 0 if not applicable or not found.
 */
int platform_get_hw_device_for_codec(const char* codec,
                                     char* hw_device,
                                     size_t hw_device_sz);

/* ---------------------------------------------------------------
 *  Utilities
 * --------------------------------------------------------------- */

/**
 * Returns the number of logical CPU cores.
 */
int platform_get_cpu_count(void);

/**
 * Probes the first video stream of input_path via ffprobe.
 * Fills width, height, fps on success.
 * Returns 1 on success, 0 on failure or not applicable.
 * On platforms where this is not needed (Linux), returns 0 without error.
 */
int platform_get_video_info(const char* input_path,
                             int* width, int* height, double* fps);

/* ---------------------------------------------------------------
 *  Platform capability flags
 * --------------------------------------------------------------- */

#define PLAT_CAP_VAAPI_H264      (1 << 0)
#define PLAT_CAP_VAAPI_HEVC      (1 << 1)
#define PLAT_CAP_VIDEOTOOLBOX    (1 << 2)
#define PLAT_CAP_NVENC_H264      (1 << 3)
#define PLAT_CAP_NVENC_HEVC      (1 << 4)
#define PLAT_CAP_QSV_H264        (1 << 5)
#define PLAT_CAP_QSV_HEVC        (1 << 6)
#define PLAT_CAP_LIBFDK_AAC      (1 << 7)
#define PLAT_CAP_AAC_AT          (1 << 8)  /* macOS only */
#define PLAT_CAP_AMF_H264        (1 << 9)  /* Windows AMD AMF */
#define PLAT_CAP_AMF_HEVC        (1 << 10) /* Windows AMD AMF */
#define PLAT_CAP_VULKAN_PRORES   (1 << 11) /* prores_ks_vulkan (GPU-accelerated ProRes via Vulkan) */
#define PLAT_CAP_AV1_QSV_DEC    (1 << 12) /* av1_qsv decoder available (Intel QSV/D3D11VA AV1 decode) */
#define PLAT_CAP_LIBDAV1D_DEC   (1 << 13) /* libdav1d decoder available (pure software AV1 decode) */
#define PLAT_CAP_AMF_AV1        (1 << 14) /* av1_amf (AMD AMF AV1 encode, RDNA3+) */
#define PLAT_CAP_VULKAN_H264    (1 << 15) /* h264_vulkan (hardware Vulkan video encode) */
#define PLAT_CAP_VULKAN_HEVC    (1 << 16) /* hevc_vulkan (hardware Vulkan video encode) */
#define PLAT_CAP_VULKAN_AV1     (1 << 17) /* av1_vulkan (hardware Vulkan video encode) */

/**
 * Returns pre-input hardware device initialization flags for the given codec.
 * Called once before -i in build_ffmpeg_cmd.
 * Example (Windows Vulkan): "-init_hw_device vulkan=vk:0 -filter_hw_device vk "
 * Returns NULL if no pre-input flags are needed for this codec.
 * The returned pointer is valid until the next call from the same thread.
 */
const char* platform_get_preinput_hw_flags(const char* codec,
                                            const void* opts);

/**
 * Returns the hwupload video filter string for hw-accelerated codecs.
 * Used in the -vf option for VAAPI and Vulkan codecs.
 * Example (Linux VAAPI):    "format=nv12,hwupload"
 * Example (Windows Vulkan): "format=yuv422p10le,hwupload"
 * Returns NULL to fall back to the default ("format=nv12,hwupload").
 * The returned pointer is valid until the next call from the same thread.
 */
const char* platform_get_hw_vfilter(const char* codec, const void* opts);

#ifdef __cplusplus
}
#endif

#endif /* CONVERTER_PLATFORM_H */
