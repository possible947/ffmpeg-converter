#ifndef WIN_PLATFORM_H
#define WIN_PLATFORM_H

/*
 * platform.h — Windows platform abstraction layer
 *
 * Declares Windows-specific helper functions that wrap Windows API calls
 * for common operations like directory creation, path resolution, and
 * tool discovery. These functions are only compiled on Windows (_WIN32).
 *
 * Usage: Include this header in Windows-specific source files that need
 * platform-level services. For cross-platform code, use compat.h instead.
 */

#ifdef _WIN32

#include <stddef.h>  /* size_t */

/*
 * win_mkdir_p — recursively create a directory path on Windows.
 *
 * Equivalent to `mkdir -p` on POSIX systems. Creates all intermediate
 * directories using CreateDirectoryA(). Existing directories are not
 * treated as errors.
 *
 * Parameters:
 *   path  — null-terminated path to create (may use / or \ separators)
 *
 * Returns:
 *   0 on success (directory exists or was created)
 *  -1 on failure (GetLastError() contains the Windows error code)
 */
int win_mkdir_p(const char *path);

/*
 * win_get_home_dir — retrieve the current user's home directory.
 *
 * Reads the %USERPROFILE% environment variable, which Windows sets to
 * the user's home directory (e.g. C:\Users\Alice).
 *
 * Parameters:
 *   buf   — output buffer to receive the path
 *   size  — size of buf in bytes
 *
 * Returns:
 *   0 on success
 *  -1 if %USERPROFILE% is not set or buf is too small
 */
int win_get_home_dir(char *buf, size_t size);

/*
 * win_path_exists — check whether a filesystem path exists.
 *
 * Uses GetFileAttributesA() to query the path without opening it.
 *
 * Parameters:
 *   path  — null-terminated path to test
 *
 * Returns:
 *   1 if the path exists (file or directory)
 *   0 if the path does not exist or is inaccessible
 */
int win_path_exists(const char *path);

/*
 * win_path_is_directory — check whether a path refers to a directory.
 *
 * Uses GetFileAttributesA() and tests FILE_ATTRIBUTE_DIRECTORY.
 *
 * Parameters:
 *   path  — null-terminated path to test
 *
 * Returns:
 *   1 if the path is a directory
 *   0 otherwise
 */
int win_path_is_directory(const char *path);

/*
 * win_path_is_writable — check whether a directory is writable.
 *
 * Attempts to create and immediately delete a temporary file inside the
 * given directory to determine write access without relying on ACL APIs.
 *
 * Parameters:
 *   path  — null-terminated path to the directory
 *
 * Returns:
 *   1 if the directory is writable
 *   0 if not writable or the path is not a directory
 */
int win_path_is_writable(const char *path);

/*
 * win_find_tool — search PATH for an executable tool.
 *
 * Uses SearchPathA() to locate the tool. Automatically appends ".exe" if
 * the tool name does not already end with that extension.
 *
 * Parameters:
 *   tool  — tool base name (e.g. "ffmpeg" or "mkvmerge")
 *   buf   — output buffer to receive the full resolved path
 *   size  — size of buf in bytes
 *
 * Returns:
 *   0 on success (buf contains the absolute path to the tool)
 *  -1 if the tool was not found in PATH
 */
int win_find_tool(const char *tool, char *buf, size_t size);

/*
 * win_resolve_executable_dir — get the directory of the running executable.
 *
 * Uses GetModuleFileNameA() to query the path of the current process image,
 * then strips the filename component to return the containing directory.
 *
 * Parameters:
 *   buf   — output buffer to receive the directory path (no trailing slash)
 *   size  — size of buf in bytes
 *
 * Returns:
 *   0 on success
 *  -1 on failure (e.g. buffer too small)
 */
int win_resolve_executable_dir(char *buf, size_t size);

/*
 * win_normalize_path — convert forward slashes to backslashes in-place.
 *
 * Windows API functions accept both separators, but some tools and
 * display contexts require native backslashes. This function modifies
 * the string in place.
 *
 * Parameters:
 *   path  — null-terminated path to normalize (modified in place)
 */
void win_normalize_path(char *path);

#endif /* _WIN32 */

#endif /* WIN_PLATFORM_H */
