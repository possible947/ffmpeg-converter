/**
 * mux_platform.h
 * Platform abstraction interface for the MUX post-processing module.
 * All platform-specific operations are declared here and implemented in
 * platform/mux_platform_posix.c (Linux + macOS).
 *
 * Rules:
 *  - No platform #ifdef in this file
 *  - No implementation in this file (header only)
 *  - Every function must be implemented on every supported platform
 */

#ifndef FFMPEG_CONVERTER_MUX_PLATFORM_H
#define FFMPEG_CONVERTER_MUX_PLATFORM_H

#include <stdio.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------
 *  PATH_MAX (POSIX defines it)
 * --------------------------------------------------------------- */
#ifndef PATH_MAX
#  define PATH_MAX 4096
#endif

/* ---------------------------------------------------------------
 *  File system
 * --------------------------------------------------------------- */

/**
 * platform_file_is_regular() — Returns 1 if path refers to a regular file
 * that exists and can be read.
 * POSIX: stat() + S_ISREG
 */
int platform_file_is_regular(const char *path);

/**
 * platform_unlink() — Remove a file.
 * POSIX: unlink()
 * Returns 0 on success, -1 on error (like the underlying calls).
 */
int platform_unlink(const char *path);

/* ---------------------------------------------------------------
 *  Process execution
 * --------------------------------------------------------------- */

/**
 * mux_platform_popen() — Open a process pipe for reading.
 * POSIX: popen(cmd, mode)
 */
FILE *mux_platform_popen(const char *cmd, const char *mode);

/**
 * platform_pclose_exitcode() — Close a process pipe and return the
 * process exit code.
 * POSIX: pclose() + WIFEXITED / WEXITSTATUS
 * Returns the exit code on success, or -1 on error.
 */
int platform_pclose_exitcode(FILE *fp);

/* ---------------------------------------------------------------
 *  Shell helpers
 * --------------------------------------------------------------- */

/**
 * platform_null_redirect() — Returns the shell redirect string used to
 * suppress stderr output.
 * POSIX: "2>/dev/null"
 */
const char *platform_null_redirect(void);

/**
 * platform_shell_quote() — Write a safely-quoted version of input into
 * out (at most out_sz bytes including the NUL terminator).
 * POSIX: single-quote with '\'' escaping
 */
void platform_shell_quote(const char *input, char *out, size_t out_sz);

/**
 * platform_rename() — Rename (move) a file from src to dst, replacing dst
 * if it already exists.
 * POSIX: rename() (atomic replace)
 * Returns 0 on success, -1 on error.
 */
int platform_rename(const char *src, const char *dst);

/**
 * platform_get_ffprobe_bin() and platform_get_mkvmerge_bin() are provided
 * by the converter library (converter_platform.h / platform_get_*).
 * mux.c includes converter_platform.h directly to call them.
 */

#ifdef __cplusplus
}
#endif

#endif /* FFMPEG_CONVERTER_MUX_PLATFORM_H */
