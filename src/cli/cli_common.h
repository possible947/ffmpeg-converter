/**
 * cli_common.h
 * Declarations for platform-agnostic CLI helpers.
 * All functions here are implemented in cli_common.c and are
 * shared across Linux, macOS, and Windows.
 */

#ifndef CLI_COMMON_H
#define CLI_COMMON_H

#include <stddef.h>
#include "converter.h"
#include "cli_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLI_BUFFER_SIZE 4096

/* Version string shown by --version.  Keep in sync with CMakeLists.txt
 * project(VERSION ...) and the [Unreleased]/latest release in CHANGELOG.md. */
#define FFMPEG_CONVERTER_VERSION "2.6.0"

/* ---------------------------------------------------------------
 *  CLI Callbacks (passed to converter_set_callbacks)
 * --------------------------------------------------------------- */

void cli_on_file_begin(const char* filename, int index, int total);
void cli_on_file_end(const char* filename, ConverterError status);
void cli_on_stage(const char* stage);
void cli_on_progress_encode(float percent, float fps, float eta);
void cli_on_progress_analysis(float percent, float eta);
void cli_on_message(const char* text);
void cli_on_error(const char* text, ConverterError code);
void cli_on_complete(void);

/* ---------------------------------------------------------------
 *  Apple M4V options (platform-agnostic mirror of M4VOptions)
 *  Filled by parse_args / run_menu; copied into M4VOptions in main.c.
 * --------------------------------------------------------------- */

typedef struct {
    int  video_track_index; /* 0-based index of the video stream to use */
    int  audio_track_index; /* 0-based index of the audio stream to use */
    int  ac3_bitrate_kbps;  /* AC3 bitrate in kbps (default 640)        */
    char audio_lang[16];    /* ISO 639 language tag (default "rus")      */
    int  add_chapters;      /* 1 = embed chapter markers (default 1)    */
} CliM4VOptions;

/* ---------------------------------------------------------------
 *  Display helpers
 * --------------------------------------------------------------- */

void clear_screen(void);
void print_usage(const CliPlatformHandle* h);
void print_version(void);
void print_summary(const ConvertOptions* opts,
                   const CliM4VOptions* m4v_opts,
                   const char** files, int file_count);

/* ---------------------------------------------------------------
 *  Input helpers
 * --------------------------------------------------------------- */

/** Reads a single character choice from stdin (newline = default). */
int read_choice(void);

/**
 * Strips shell quoting and escape characters from an input path.
 * Returns 1 on success, 0 on error.
 */
int process_input_path(const char* input, char* output, size_t out_size);

/**
 * Interactively reads a list of file paths from stdin.
 * Allocates *out_files (caller must free each element and the array).
 * Returns 0 on success, -1 on cancel/error.
 */
int read_input_list(char*** out_files, int max_cnt, int* count);

/**
 * Interactively reads and validates an output directory path.
 * Uses platform_get_home_dir() for the default.
 * Returns 0 on success, -1 on cancel/error.
 */
int read_output_dir(char* out_buf, size_t bufsize, int* status);

/**
 * Reads a single file path with validation.
 * Returns 0 on success, -1 on cancel/error.
 */
int read_single_file_path(const char* prompt,
                          char* out_path, size_t out_path_sz);

/* ---------------------------------------------------------------
 *  File validation
 * --------------------------------------------------------------- */

/**
 * Checks all files for readability.  Returns the count of valid files,
 * or 0 if none are valid.
 */
int verify_all_files(const char** files, int file_count);

/* ---------------------------------------------------------------
 *  Argument parsing and interactive menu
 * --------------------------------------------------------------- */

/**
 * Parses command-line arguments into opts and files[].
 * Uses platform functions for codec/audio-mode validation.
 * Returns 1 on success (continue), 0 on error/help shown.
 */
int parse_args(int argc, char** argv, const CliPlatformHandle* h,
               ConvertOptions* opts, CliM4VOptions* m4v_opts,
               const char** files, int* file_count);

/**
 * Runs the interactive step-by-step menu.
 * On success, fills opts, m4v_opts, *files_ptr, and *file_count.
 * Returns 0 on success, -1 on cancel/error.
 */
int run_menu(const CliPlatformHandle* h, ConvertOptions* opts,
             CliM4VOptions* m4v_opts,
             const char*** files_ptr, int* file_count);

/* ---------------------------------------------------------------
 *  Codec/Preset listing and validation (Task 4)
 * --------------------------------------------------------------- */

/**
 * Prints all available codecs and their presets for the current platform.
 * Only codecs that pass the runtime hardware/tool probe (as reported by
 * platform_codec_is_available()) are listed — matching the codec set shown
 * in --help and the interactive menu. `h` may be NULL, in which case only
 * probe-independent codecs (copy/prores/prores_ks/mux/m4v) are listed.
 */
void cli_print_codecs_list(const CliPlatformHandle* h);

/**
 * Validates that a codec/preset combination is valid for the current platform.
 * Returns 1 if valid, 0 if not.
 * On error, prints error message to stderr.
 */
int cli_validate_codec_preset(const char* codec, const char* preset);

/**
 * Gets the preset choice menu for a specific codec.
 * Returns the selected preset name, or NULL on cancel.
 * Caller must free the returned string.
 */
char* cli_choose_preset_for_codec(const char* codec);

#ifdef __cplusplus
}
#endif

#endif /* CLI_COMMON_H */
