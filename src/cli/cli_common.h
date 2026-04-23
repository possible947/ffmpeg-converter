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
 *  Display helpers
 * --------------------------------------------------------------- */

void clear_screen(void);
void print_usage(const CliPlatformHandle* h);
void print_summary(const ConvertOptions* opts,
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
               ConvertOptions* opts,
               const char** files, int* file_count);

/**
 * Runs the interactive step-by-step menu.
 * On success, fills opts, *files_ptr, and *file_count.
 * Returns 0 on success, -1 on cancel/error.
 */
int run_menu(const CliPlatformHandle* h, ConvertOptions* opts,
             const char*** files_ptr, int* file_count);

#ifdef __cplusplus
}
#endif

#endif /* CLI_COMMON_H */
