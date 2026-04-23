/**
 * platform/m4v_platform_windows.c
 * Windows implementation of the m4v platform abstraction.
 *
 * Implements the m4v_platform.h interface using:
 *  - GetFileAttributesA() for file existence checks
 *  - GetTempPathA() + CreateDirectoryA() for temporary directory creation
 *  - FindFirstFileA / DeleteFileA / RemoveDirectoryA for recursive removal
 *  - _popen() / _pclose() for process execution
 *  - Double-quote cmd.exe (CommandLineToArgvW) shell quoting
 *  - _unlink() for file removal
 *
 * Binary resolution (platform_get_ffmpeg_bin / platform_get_ffprobe_bin /
 * platform_get_mp4box_bin) is provided by the converter library via
 * converter_platform.h and is not reimplemented here.
 */

#include "../m4v_platform.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>     /* _unlink */

/* ---------------------------------------------------------------
 *  File system
 * --------------------------------------------------------------- */

int m4v_platform_is_regular_file(const char *path)
{
    DWORD attrs;

    if (!path || path[0] == '\0')
        return 0;

    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return 0;

    /* Must not be a directory */
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

int m4v_platform_file_exists(const char *path)
{
    if (!path || path[0] == '\0')
        return 0;
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

int m4v_platform_unlink(const char *path)
{
    return _unlink(path);
}

/* ---------------------------------------------------------------
 *  Temp directory management
 * --------------------------------------------------------------- */

int m4v_platform_make_temp_dir(char *path, size_t path_sz)
{
    char tmp_base[MAX_PATH];
    char dir_path[MAX_PATH];
    DWORD rv;
    DWORD pid;
    static volatile LONG s_counter = 0;
    LONG count;

    if (!path || path_sz == 0)
        return 0;

    rv = GetTempPathA(sizeof(tmp_base), tmp_base);
    if (rv == 0 || rv >= sizeof(tmp_base))
        return 0;

    pid   = GetCurrentProcessId();
    count = InterlockedIncrement(&s_counter);

    /* Build a unique directory name: <TempPath>m4v_mux_<PID>_<counter> */
    snprintf(dir_path, sizeof(dir_path), "%sm4v_mux_%lu_%lu",
             tmp_base, (unsigned long)pid, (unsigned long)count);

    if (!CreateDirectoryA(dir_path, NULL))
        return 0;

    strncpy(path, dir_path, path_sz - 1);
    path[path_sz - 1] = '\0';
    return 1;
}

/* Recursive helper — removes all contents of dir, then dir itself */
static void win_rmtree(const char *path)
{
    WIN32_FIND_DATAA ffd;
    HANDLE hFind;
    char pattern[MAX_PATH];
    char child[MAX_PATH];

    if (!path || path[0] == '\0')
        return;

    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    hFind = FindFirstFileA(pattern, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        RemoveDirectoryA(path);
        return;
    }

    do {
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0)
            continue;
        snprintf(child, sizeof(child), "%s\\%s", path, ffd.cFileName);
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            win_rmtree(child);
        else
            DeleteFileA(child);
    } while (FindNextFileA(hFind, &ffd));

    FindClose(hFind);
    RemoveDirectoryA(path);
}

void m4v_platform_remove_temp_dir(const char *dir)
{
    if (!dir || dir[0] == '\0')
        return;
    win_rmtree(dir);
}

/* ---------------------------------------------------------------
 *  Process execution
 * --------------------------------------------------------------- */

FILE *m4v_platform_popen(const char *cmd, const char *mode)
{
    return _popen(cmd, mode);
}

int m4v_platform_pclose_exitcode(FILE *fp)
{
    /* _pclose() returns the process exit code directly on Windows */
    return _pclose(fp);
}

/* ---------------------------------------------------------------
 *  Shell helpers
 * --------------------------------------------------------------- */

const char *m4v_platform_null_redirect(void)
{
    return "2>nul";
}

/**
 * m4v_platform_shell_quote() — Windows cmd.exe double-quote quoting.
 *
 * Strategy: wrap the argument in double quotes.  The escaping rules follow
 * CommandLineToArgvW() parsing:
 *  - Backslashes are literal unless immediately followed by a double-quote.
 *  - Backslashes immediately before a double-quote must be doubled.
 *  - Backslashes at the very end of the string (before the closing quote)
 *    must be doubled.
 *  - A literal double-quote is escaped as \".
 *
 * This is the standard quoting algorithm described by Microsoft.
 */
void m4v_platform_shell_quote(const char *input, char *out, size_t out_sz)
{
    size_t      pos = 0;
    const char *p;
    size_t      n_bs;
    size_t      i;

    if (!out || out_sz == 0)
        return;

    if (!input)
        input = "";

    if (out_sz < 3) {
        out[0] = '\0';
        return;
    }

    /* Opening double-quote; reserve 2 chars at the end for '"' + '\0' */
    out[pos++] = '"';

    for (p = input; *p != '\0'; ) {
        /* Count consecutive backslashes starting at p */
        n_bs = 0;
        while (p[n_bs] == '\\')
            ++n_bs;

        if (p[n_bs] == '"') {
            /* Backslashes before a double-quote: emit 2*N backslashes,
             * then \" to represent a literal double-quote */
            for (i = 0; i < n_bs * 2 && pos + 2 < out_sz; ++i)
                out[pos++] = '\\';
            if (pos + 2 < out_sz) {
                out[pos++] = '\\';
                out[pos++] = '"';
            }
            p += n_bs + 1;
        } else if (p[n_bs] == '\0') {
            /* Backslashes at end of string: emit 2*N backslashes so the
             * closing quote is not escaped */
            for (i = 0; i < n_bs * 2 && pos + 2 < out_sz; ++i)
                out[pos++] = '\\';
            p += n_bs;
        } else {
            /* Backslashes not before a double-quote: emit as-is, then char */
            for (i = 0; i < n_bs && pos + 2 < out_sz; ++i)
                out[pos++] = '\\';
            if (pos + 2 < out_sz)
                out[pos++] = p[n_bs];
            p += n_bs + 1;
        }
    }

    if (pos + 1 < out_sz) {
        out[pos++] = '"';
        out[pos] = '\0';
    } else {
        /* Buffer too small; ensure NUL termination */
        out[out_sz - 1] = '\0';
    }
}
