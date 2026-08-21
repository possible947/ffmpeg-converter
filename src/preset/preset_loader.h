#ifndef PRESET_LOADER_H
#define PRESET_LOADER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
//  Preset Info Structure
// ============================================================================

typedef struct {
    char *ffmpeg_args;      // Required: codec and quality flags
    char *container;        // Required: mkv, mov, mp4, m4v
    char *pix_fmt;          // Optional: pixel format
    char *pre_input_args;   // Optional: pre-input arguments (e.g., -vaapi_device)
    char *video_filter;     // Optional: video filter (e.g., hwupload)
    char *pipeline;         // Optional: special pipeline type (e.g., "m4v_direct")
    char **requires;        // Optional: array of required tools
    size_t requires_count;  // Number of required tools
} PresetInfo;

// ============================================================================
//  Opaque Preset Database Handle
// ============================================================================

typedef struct PresetDb PresetDb;

// ============================================================================
//  Public API
// ============================================================================

/**
 * Load preset database from file system.
 * 
 * Searches for presets.json in the following order:
 *   1. Executable-adjacent directory (./presets.json)
 *   2. User config directory ($HOME/.config/ffmpeg_converter/presets.json on Linux,
 *      ~/Library/Preferences/ffmpeg_converter/presets.json on macOS,
 *      %APPDATA%\ffmpeg_converter\presets.json on Windows)
 *   3. Built-in fallback (minimal set: copy/prores/prores_ks with default presets)
 *
 * If presets_path is not NULL, search starts from that directory first.
 *
 * @param presets_path  Optional explicit path to presets.json (or directory containing it)
 * @return              Pointer to PresetDb on success, NULL on fatal error
 *                      (fallback to built-in only fails if malloc fails)
 */
PresetDb *preset_db_load(const char *presets_path);

/**
 * Get preset info from database.
 *
 * Performs O(1) lookup by platform, codec, and preset name.
 *
 * @param db            Database handle from preset_db_load()
 * @param os_name       Platform: "linux", "macos", or "windows"
 * @param codec_name    Codec name (e.g., "prores_ks", "h264_vaapi")
 * @param preset_name   Preset name (e.g., "hq", "default")
 * @return              Pointer to PresetInfo on success, NULL if not found
 *                      (returned pointer is valid until preset_db_free())
 */
const PresetInfo *preset_db_get(PresetDb *db, const char *os_name,
                                 const char *codec_name, const char *preset_name);

/**
 * Get list of available codecs for a platform.
 *
 * @param db            Database handle from preset_db_load()
 * @param os_name       Platform: "linux", "macos", or "windows"
 * @param out_codecs    Output array of codec name strings (caller does not free)
 * @return              Number of codecs available for the platform
 */
int preset_db_list_codecs(PresetDb *db, const char *os_name,
                          const char ***out_codecs);

/**
 * Get list of available presets for a specific codec.
 *
 * @param db            Database handle from preset_db_load()
 * @param os_name       Platform: "linux", "macos", or "windows"
 * @param codec_name    Codec name
 * @param out_presets   Output array of preset name strings (caller does not free)
 * @return              Number of presets available for the codec
 */
int preset_db_list_presets(PresetDb *db, const char *os_name,
                           const char *codec_name, const char ***out_presets);

/**
 * Substitute runtime placeholders in a preset string.
 *
 * Supported placeholders:
 *   {vaapi_device}   → VAAPI render device path (default: /dev/dri/renderD128)
 *   {vk_device}      → Vulkan device index (default: 0)
 *   {vt_bitrate}     → VideoToolbox bitrate in kbps (must be provided)
 *
 * @param output        Output buffer (must be >= size bytes)
 * @param size          Size of output buffer
 * @param template      Input template string with placeholders
 * @param vaapi_device  VAAPI device path (NULL for default)
 * @param vk_device     Vulkan device index (use -1 for default)
 * @param vt_bitrate    VideoToolbox bitrate in kbps (use 0 if not applicable)
 * @return              0 on success, -1 if output buffer too small or invalid input
 */
int preset_substitute_placeholders(char *output, size_t size,
                                   const char *template,
                                   const char *vaapi_device,
                                   int vk_device,
                                   int vt_bitrate);

/**
 * Free preset database and all associated resources.
 *
 * @param db            Database handle from preset_db_load()
 */
void preset_db_free(PresetDb *db);

/**
 * Get last error message from preset loader.
 *
 * @return              Human-readable error string (never NULL)
 */
const char *preset_get_last_error(void);

#ifdef __cplusplus
}
#endif

#endif // PRESET_LOADER_H
