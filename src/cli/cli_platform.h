/**
 * cli_platform.h
 * Platform abstraction interface for the CLI entry point.
 * All platform-specific operations are declared here and implemented
 * in platform/cli_{linux,macos,windows}.c
 *
 * Rules:
 *  - No platform #ifdef in this file
 *  - No implementation in this file (header only)
 *  - Every function must be implemented on every supported platform
 */

#ifndef CLI_PLATFORM_H
#define CLI_PLATFORM_H

#include <stddef.h>
#include "converter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------
 *  Opaque platform handle
 * --------------------------------------------------------------- */

/**
 * CliPlatformHandle — opaque struct defined privately in each
 * platform/cli_*.c file.  Callers treat it as a black box.
 */
typedef struct CliPlatformHandle CliPlatformHandle;

/* ---------------------------------------------------------------
 *  Codec entry for the interactive menu
 * --------------------------------------------------------------- */

typedef struct {
    const char* name;         /* codec string, e.g. "prores_ks"  */
    int         needs_profile; /* 1 = show profile selection step */
    int         needs_deblock; /* 1 = show deblock selection step */
} PlatformCodecEntry;

/* ---------------------------------------------------------------
 *  Lifecycle
 * --------------------------------------------------------------- */

/**
 * cli_platform_init() — Probe GPU/codec support.
 * Returns an opaque handle on success, NULL on fatal error.
 * The caller owns the handle and must pass it to cli_platform_cleanup().
 */
CliPlatformHandle* cli_platform_init(void);

/**
 * cli_platform_cleanup() — Release resources from cli_platform_init().
 * Safe to call with h == NULL.
 */
void cli_platform_cleanup(CliPlatformHandle* h);

/* ---------------------------------------------------------------
 *  Codec / audio-mode availability
 * --------------------------------------------------------------- */

/**
 * platform_codec_is_available() — Returns 1 if the codec string is
 * supported on this platform with the detected hardware.
 */
int platform_codec_is_available(const CliPlatformHandle* h,
                                const char* codec);

/**
 * platform_audio_mode_is_available() — Returns 1 if the audio output
 * mode string is valid on this platform.
 */
int platform_audio_mode_is_available(const char* mode);

/**
 * platform_mux_is_supported() — Returns 1 if "mux" mode is available
 * (requires mkvmerge; currently Linux only).
 */
int platform_mux_is_supported(void);

/**
 * platform_m4v_is_supported() — Returns 1 if Apple M4V creation is
 * available (requires MP4Box found on PATH or next to the executable).
 * Windows only; always returns 0 on other platforms.
 */
int platform_m4v_is_supported(void);

/**
 * platform_get_default_vulkan_device() — Returns the recommended Vulkan
 * device index for the given codec, based on the startup probe result.
 * `codec` may be NULL (e.g. generic help text printed before any codec is
 * chosen), in which case the legacy prores_ks_vulkan-first priority is used.
 * For "h264_vulkan"/"hevc_vulkan"/"av1_vulkan" the hardware-encoder probe
 * result (vulkan_hw_device_index) is preferred over the ProRes-only probe,
 * since the two encoder families can succeed on different physical GPUs.
 * Returns the highest-indexed device that passed the relevant probe test
 * (statistically more likely to be a discrete GPU than vk:0).
 * Returns 1 as a safe fallback if no probe data is available.
 * Returns 0 on platforms that do not support Vulkan (macOS).
 */
int platform_get_default_vulkan_device(const CliPlatformHandle* h, const char* codec);

/* ---------------------------------------------------------------
 *  Interactive menu codec list
 * --------------------------------------------------------------- */

/**
 * platform_get_codec_count() — Returns the number of codec entries
 * available in the interactive menu on this platform/hardware.
 */
int platform_get_codec_count(const CliPlatformHandle* h);

/**
 * platform_get_codec_entries() — Returns a pointer to the array of
 * PlatformCodecEntry structs.  The array is valid for the lifetime of h.
 */
const PlatformCodecEntry* platform_get_codec_entries(const CliPlatformHandle* h);

/* ---------------------------------------------------------------
 *  Hardware device defaults
 * --------------------------------------------------------------- */

/**
 * platform_apply_hw_device() — Sets opts->hw_device based on the selected codec.
 * No-op on platforms without hardware acceleration.
 */
void platform_apply_hw_device(ConvertOptions* opts,
                              const CliPlatformHandle* h);

/* ---------------------------------------------------------------
 *  Home directory
 * --------------------------------------------------------------- */

/**
 * platform_get_home_dir() — Returns the current user's home directory
 * path, or "." if unavailable.
 * The returned pointer is valid for the lifetime of the process.
 *
 * NOTE: This is a separate declaration from the converter library's
 * platform_get_home_dir().  CLI platform files implement this function
 * under the name cli_get_home_dir() to avoid link-time conflicts.
 */
const char* cli_get_home_dir(void);

/* ---------------------------------------------------------------
 *  File / directory helpers
 * --------------------------------------------------------------- */

/**
 * platform_file_is_regular_readable() — Returns 1 if path refers to
 * a regular, readable file.
 */
int platform_file_is_regular_readable(const char* path);

/**
 * platform_dir_is_writable() — Returns 1 if path is an existing
 * directory that the process can write to.
 */
int platform_dir_is_writable(const char* path);

/**
 * platform_ensure_output_dir() — Creates path if it doesn't exist,
 * then checks write access.
 * Returns 1 on success (exists and writable), 0 on failure.
 */
int platform_ensure_output_dir(const char* path);

/* ---------------------------------------------------------------
 *  Mux post-processing
 * --------------------------------------------------------------- */

/**
 * platform_run_mux_postprocess() — Runs the mkvmerge mux step after
 * the main ffmpeg conversion (Linux only).
 * Returns ERR_INVALID_OPTIONS on platforms where mux is not supported.
 */
ConverterError platform_run_mux_postprocess(const ConvertOptions* opts,
                                            const ConverterCallbacks* cb,
                                            const char* input_file);

/**
 * platform_utf8_argv() — Returns an argv array where every string is
 * UTF-8 encoded.  On Windows this uses GetCommandLineW / CommandLineToArgvW
 * to bypass the ANSI code-page conversion applied by the C runtime, so file
 * paths with non-ANSI characters (Cyrillic, CJK, special symbols) arrive
 * intact.  On Linux / macOS the process already receives UTF-8 argv, so the
 * function is a no-op that returns the original argv unchanged.
 *
 * If *needs_free is set to 1 on return, the caller must free each
 * result[i] and then result itself when argv is no longer needed.
 */
char** platform_utf8_argv(int argc, char** argv, int* needs_free);

#ifdef __cplusplus
}
#endif

#endif /* CLI_PLATFORM_H */
