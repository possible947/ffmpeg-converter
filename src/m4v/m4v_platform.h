/**
 * m4v_platform.h
 * Platform abstraction interface for the M4V module.
 * All platform-specific operations are declared here and implemented in
 * platform/m4v_platform_posix.c (Linux + macOS) and
 * platform/m4v_platform_windows.c (Windows).
 *
 * Rules:
 *  - No platform #ifdef in this file
 *  - No implementation in this file (header only)
 *  - Every function must be implemented on every supported platform
 */

#ifndef M4V_PLATFORM_H
#define M4V_PLATFORM_H

#include <stdio.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------
 *  PATH_MAX fallback (POSIX defines it; Windows MSVC does not)
 * --------------------------------------------------------------- */
#ifndef PATH_MAX
#  define PATH_MAX 4096
#endif

/* ---------------------------------------------------------------
 *  strtok_r / strtok_s compatibility shim
 * --------------------------------------------------------------- */
#if defined(_MSC_VER)
#  define strtok_r strtok_s
#endif

/* ---------------------------------------------------------------
 *  File system
 * --------------------------------------------------------------- */

/**
 * m4v_platform_is_regular_file() — Returns 1 if path refers to a regular
 * file that exists and can be read.
 * POSIX: stat() + S_ISREG + access(R_OK)
 * Windows: GetFileAttributesA() without FILE_ATTRIBUTE_DIRECTORY
 */
int m4v_platform_is_regular_file(const char *path);

/**
 * m4v_platform_file_exists() — Returns 1 if path exists (any file type).
 * POSIX: access(F_OK)
 * Windows: GetFileAttributesA() != INVALID_FILE_ATTRIBUTES
 */
int m4v_platform_file_exists(const char *path);

/**
 * m4v_platform_unlink() — Remove a file.
 * POSIX: unlink()
 * Windows: _unlink()
 * Returns 0 on success, -1 on error.
 */
int m4v_platform_unlink(const char *path);

/* ---------------------------------------------------------------
 *  Temp directory management
 * --------------------------------------------------------------- */

/**
 * m4v_platform_make_temp_dir() — Create a unique temporary directory and
 * store its path in the caller-supplied buffer.
 * POSIX: mkdtemp() with /tmp/m4v_mux_XXXXXX template
 * Windows: GetTempPathA() + CreateDirectoryA()
 * Returns 1 on success, 0 on failure.
 */
int m4v_platform_make_temp_dir(char *path, size_t path_sz);

/**
 * m4v_platform_remove_temp_dir() — Recursively remove a temporary directory
 * and all its contents.
 * POSIX: recursive opendir/unlink/rmdir
 * Windows: FindFirstFileA/DeleteFileA/RemoveDirectoryA
 */
void m4v_platform_remove_temp_dir(const char *dir);

/* ---------------------------------------------------------------
 *  Process execution
 * --------------------------------------------------------------- */

/**
 * m4v_platform_popen() — Open a process pipe for reading.
 * POSIX: popen(cmd, mode)
 * Windows: _popen(cmd, mode)
 */
FILE *m4v_platform_popen(const char *cmd, const char *mode);

/**
 * m4v_platform_pclose_exitcode() — Close a process pipe and return the
 * process exit code.
 * POSIX: pclose() + WIFEXITED / WEXITSTATUS
 * Windows: _pclose() (returns exit code directly)
 * Returns the exit code on success, or -1 on error.
 */
int m4v_platform_pclose_exitcode(FILE *fp);

/* ---------------------------------------------------------------
 *  Shell helpers
 * --------------------------------------------------------------- */

/**
 * m4v_platform_null_redirect() — Returns the shell redirect string used to
 * suppress stderr output.
 * POSIX: "2>/dev/null"
 * Windows: "2>nul"
 */
const char *m4v_platform_null_redirect(void);

/**
 * m4v_platform_shell_quote() — Write a safely-quoted version of input into
 * out (at most out_sz bytes including the NUL terminator).
 * POSIX: single-quote with '\'' escaping
 * Windows: double-quote with cmd.exe (CommandLineToArgvW) escaping
 */
void m4v_platform_shell_quote(const char *input, char *out, size_t out_sz);

#ifdef __cplusplus
}
#endif

#endif /* M4V_PLATFORM_H */
