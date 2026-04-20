/*
 * platform.c — Windows platform abstraction layer implementation
 *
 * Implements the Windows-specific helper functions declared in platform.h.
 * All functions use the Windows API (windows.h) and are only compiled when
 * _WIN32 is defined (i.e. on MinGW/MSYS2 or MSVC Windows builds).
 *
 * Windows API references used here:
 *   CreateDirectoryA  — create a single directory component
 *   GetFileAttributesA — query file/directory attributes without opening
 *   GetModuleFileNameA — retrieve the path of the running executable
 *   SearchPathA        — search PATH directories for a named executable
 *   GetLastError       — retrieve the last Windows error code
 *
 * Error handling convention:
 *   Functions return 0 on success, -1 on failure unless noted otherwise.
 *   Callers may call GetLastError() immediately after a -1 return to
 *   obtain the Windows error code for diagnostic purposes.
 */

#ifdef _WIN32

#include "platform.h"

#include <windows.h>   /* Windows API — CreateDirectoryA, GetFileAttributesA, etc. */
#include <stdio.h>     /* snprintf, tmpnam                                          */
#include <string.h>    /* strlen, strrchr, strncpy, strcmp                          */
#include <stdlib.h>    /* getenv                                                    */

/* -------------------------------------------------------------------------
 * win_mkdir_p — recursive directory creation
 *
 * Strategy: walk the path character by character. Each time a separator
 * is found, temporarily NUL-terminate the string at that position and
 * call CreateDirectoryA() on the prefix built so far.  After the loop,
 * create the full path.  ERROR_ALREADY_EXISTS is not treated as an error
 * so that existing directories are handled gracefully.
 * ---------------------------------------------------------------------- */
int win_mkdir_p(const char *path)
{
    if (!path || !*path)
        return -1;

    char buf[MAX_PATH];
    size_t len = strlen(path);
    if (len >= MAX_PATH)
        return -1;  /* path too long */

    /* Copy into mutable buffer so we can insert temporary NUL characters. */
    strncpy_s(buf, sizeof(buf), path, _TRUNCATE);
    buf[len] = '\0';

    /* Normalise separators to backslash for Windows API calls. */
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '/')
            buf[i] = '\\';
    }

    /* Walk the path and create each component. */
    for (size_t i = 1; i <= len; i++) {
        if (buf[i] == '\\' || buf[i] == '\0') {
            char saved = buf[i];
            buf[i] = '\0';

            /*
             * Skip drive-root component (e.g. "C:\") — CreateDirectoryA
             * would fail with ERROR_ACCESS_DENIED on the root itself.
             */
            if (!(i == 2 && buf[1] == ':') && !(i == 3 && buf[1] == ':' && buf[2] == '\\')) {
                BOOL ok = CreateDirectoryA(buf, NULL);
                if (!ok) {
                    DWORD err = GetLastError();
                    if (err != ERROR_ALREADY_EXISTS)
                        return -1;  /* real error — propagate */
                }
            }

            buf[i] = saved;  /* restore separator */
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * win_get_home_dir — retrieve the user home directory from %USERPROFILE%
 *
 * %USERPROFILE% is set by Windows for every logged-in user and points to
 * a path such as C:\Users\Alice.  We use getenv() here because we only
 * need an ANSI (narrow) path and it avoids the need for the Environment
 * Block APIs.
 * ---------------------------------------------------------------------- */
int win_get_home_dir(char *buf, size_t size)
{
    if (!buf || size == 0)
        return -1;

    const char *profile = getenv("USERPROFILE");
    if (!profile || !*profile)
        return -1;  /* environment variable not set */

    size_t needed = strlen(profile) + 1;
    if (needed > size)
        return -1;  /* caller's buffer too small */

    strncpy_s(buf, size, profile, _TRUNCATE);
    return 0;
}

/* -------------------------------------------------------------------------
 * win_path_exists — test whether a path exists
 *
 * GetFileAttributesA() returns INVALID_FILE_ATTRIBUTES when the path does
 * not exist or is inaccessible.  Any other return value means the path is
 * present on the filesystem.
 * ---------------------------------------------------------------------- */
int win_path_exists(const char *path)
{
    if (!path || !*path)
        return 0;

    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * win_path_is_directory — test whether a path refers to a directory
 *
 * After retrieving the file attributes, test the FILE_ATTRIBUTE_DIRECTORY
 * flag.  Symlinks to directories also carry this flag on Windows.
 * ---------------------------------------------------------------------- */
int win_path_is_directory(const char *path)
{
    if (!path || !*path)
        return 0;

    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return 0;  /* path does not exist */

    return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * win_path_is_writable — test whether a directory allows file creation
 *
 * The most reliable way to check write permission on Windows without
 * querying ACLs is to attempt creating a file.  We create a uniquely-named
 * temporary file in the target directory and delete it immediately.
 * ---------------------------------------------------------------------- */
int win_path_is_writable(const char *path)
{
    if (!win_path_is_directory(path))
        return 0;  /* not a directory — cannot be writable */

    char test_path[MAX_PATH];
    /* Build a probe filename: <path>\.wrtst_<tick> */
    DWORD tick = GetTickCount();
    int n = snprintf(test_path, sizeof(test_path), "%s\\.wrtst_%lu", path, (unsigned long)tick);
    if (n < 0 || (size_t)n >= sizeof(test_path))
        return 0;  /* path too long */

    HANDLE h = CreateFileA(
        test_path,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        NULL
    );

    if (h == INVALID_HANDLE_VALUE)
        return 0;  /* could not create file — directory not writable */

    CloseHandle(h);  /* FILE_FLAG_DELETE_ON_CLOSE removes file on close */
    return 1;
}

/* -------------------------------------------------------------------------
 * win_find_tool — locate an executable in PATH using SearchPathA
 *
 * SearchPathA() searches the directories listed in PATH (and a few other
 * standard locations) for a file whose name matches the given pattern.
 * We always pass a ".exe" extension because Windows does not automatically
 * try extensions the way cmd.exe does.
 *
 * If the caller already included ".exe" in the tool name, we use it as-is
 * to avoid ending up with ".exe.exe".
 * ---------------------------------------------------------------------- */
int win_find_tool(const char *tool, char *buf, size_t size)
{
    if (!tool || !*tool || !buf || size == 0)
        return -1;

    /* Determine whether we need to append ".exe". */
    const char *ext = NULL;
    size_t tlen = strlen(tool);
    if (tlen < 4 || _stricmp(tool + tlen - 4, ".exe") != 0)
        ext = ".exe";  /* tool name does not end in .exe — supply it */

    /*
     * SearchPathA prototype:
     *   DWORD SearchPathA(lpPath, lpFileName, lpExtension,
     *                     nBufferLength, lpBuffer, lpFilePart)
     *
     * Passing NULL for lpPath means "use the system default search order".
     * Passing the extension separately (lpExtension) lets Windows try it
     * when the filename has no extension.
     */
    char found[MAX_PATH];
    char *file_part = NULL;
    DWORD result = SearchPathA(
        NULL,       /* search PATH and standard locations              */
        tool,       /* base name (may or may not include .exe)         */
        ext,        /* extension to try — NULL if already in tool name */
        (DWORD)sizeof(found),
        found,
        &file_part
    );

    if (result == 0)
        return -1;  /* not found — GetLastError() has details          */

    if ((size_t)result >= size)
        return -1;  /* caller's buffer too small for the resolved path  */

    strncpy_s(buf, size, found, _TRUNCATE);
    return 0;
}

/* -------------------------------------------------------------------------
 * win_resolve_executable_dir — get the directory containing this executable
 *
 * GetModuleFileNameA() with a NULL module handle returns the full path of
 * the current process image (e.g. C:\tools\bin\ffmpeg_converter.exe).
 * We then strip the filename to obtain just the directory component.
 * ---------------------------------------------------------------------- */
int win_resolve_executable_dir(char *buf, size_t size)
{
    if (!buf || size == 0)
        return -1;

    char exe_path[MAX_PATH];
    DWORD n = GetModuleFileNameA(
        NULL,            /* NULL = current process executable */
        exe_path,
        (DWORD)sizeof(exe_path)
    );

    if (n == 0 || n >= sizeof(exe_path))
        return -1;  /* API failure or path truncated */

    /* Find the last backslash and trim the filename. */
    char *last_sep = strrchr(exe_path, '\\');
    if (!last_sep)
        last_sep = strrchr(exe_path, '/');  /* forward slash fallback */

    if (!last_sep)
        return -1;  /* no separator found — unexpected path format */

    *last_sep = '\0';  /* remove filename, keep directory */

    size_t dir_len = strlen(exe_path);
    if (dir_len + 1 > size)
        return -1;  /* caller's buffer too small */

    strncpy_s(buf, size, exe_path, _TRUNCATE);
    return 0;
}

/* -------------------------------------------------------------------------
 * win_normalize_path — replace forward slashes with backslashes
 *
 * Windows API functions accept forward slashes in most cases, but display
 * contexts (error messages, logs) and some tools (mkvmerge, MP4Box) work
 * best with native backslashes.  This function converts in-place.
 * ---------------------------------------------------------------------- */
void win_normalize_path(char *path)
{
    if (!path)
        return;

    for (char *p = path; *p; p++) {
        if (*p == '/')
            *p = '\\';
    }
}

#endif /* _WIN32 */
