/**
 * platform/m4v_platform_windows.c
 * Windows implementation of the m4v platform abstraction.
 *
 * Implements the m4v_platform.h interface using:
 *  - GetFileAttributesW() for file existence checks
 *  - GetTempPathW() + CreateDirectoryW() for temporary directory creation
 *  - FindFirstFileW / DeleteFileW / RemoveDirectoryW for recursive removal
 *  - _wpopen() / _pclose() for process execution
 *  - Double-quote cmd.exe (CommandLineToArgvW) shell quoting
 *  - _wunlink() for file removal
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
#include <io.h>     /* _wunlink */

/* ---------------------------------------------------------------
 *  Internal helpers
 * --------------------------------------------------------------- */

/* Convert a UTF-8 string to a newly allocated wide string.
 * Returns 1 on success (caller must free *out), 0 on failure. */
static int m4v_utf8_to_wide(const char* s, wchar_t** out) {
    int wlen;
    if (!s || !out) return 0;
    wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (wlen <= 0) return 0;
    *out = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
    if (!*out) return 0;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, *out, wlen);
    return 1;
}

/* ---------------------------------------------------------------
 *  File system
 * --------------------------------------------------------------- */

int m4v_platform_is_regular_file(const char *path)
{
    wchar_t* wpath = NULL;
    DWORD attrs;

    if (!path || path[0] == '\0')
        return 0;

    if (!m4v_utf8_to_wide(path, &wpath))
        return 0;

    attrs = GetFileAttributesW(wpath);
    free(wpath);

    if (attrs == INVALID_FILE_ATTRIBUTES)
        return 0;

    /* Must not be a directory */
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

int m4v_platform_file_exists(const char *path)
{
    wchar_t* wpath = NULL;
    DWORD attrs;

    if (!path || path[0] == '\0')
        return 0;

    if (!m4v_utf8_to_wide(path, &wpath))
        return 0;

    attrs = GetFileAttributesW(wpath);
    free(wpath);
    return attrs != INVALID_FILE_ATTRIBUTES;
}

int m4v_platform_unlink(const char *path)
{
    wchar_t* wpath = NULL;
    int result;
    if (!path) return -1;
    if (!m4v_utf8_to_wide(path, &wpath)) return _unlink(path);
    result = _wunlink(wpath);
    free(wpath);
    return result;
}

/* ---------------------------------------------------------------
 *  Temp directory management
 * --------------------------------------------------------------- */

int m4v_platform_make_temp_dir(char *path, size_t path_sz)
{
    wchar_t tmp_base[4096];
    wchar_t dir_path[4096];
    DWORD rv;
    DWORD pid;
    static volatile LONG s_counter = 0;
    LONG count;

    if (!path || path_sz == 0)
        return 0;

    rv = GetTempPathW(4096, tmp_base);
    if (rv == 0 || rv >= 4096)
        return 0;

    pid   = GetCurrentProcessId();
    count = InterlockedIncrement(&s_counter);

    /* Build a unique directory name: <TempPath>m4v_mux_<PID>_<counter> */
    _snwprintf(dir_path, 4096, L"%sm4v_mux_%lu_%lu",
               tmp_base, (unsigned long)pid, (unsigned long)count);
    dir_path[4095] = L'\0';

    if (!CreateDirectoryW(dir_path, NULL))
        return 0;

    /* Convert wide path back to UTF-8 for the caller */
    if (WideCharToMultiByte(CP_UTF8, 0, dir_path, -1,
                            path, (int)(path_sz - 1), NULL, NULL) == 0)
        return 0;
    path[path_sz - 1] = '\0';
    return 1;
}

/* Recursive helper — removes all contents of dir, then dir itself.
 * Operates entirely in wide-char to support Unicode paths. */
static void win_rmtree_w(const wchar_t *path)
{
    WIN32_FIND_DATAW ffd;
    HANDLE hFind;
    wchar_t pattern[4096];
    wchar_t child[4096];

    if (!path || path[0] == L'\0')
        return;

    _snwprintf(pattern, 4096, L"%s\\*", path);
    pattern[4095] = L'\0';
    hFind = FindFirstFileW(pattern, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        RemoveDirectoryW(path);
        return;
    }

    do {
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0)
            continue;
        _snwprintf(child, 4096, L"%s\\%s", path, ffd.cFileName);
        child[4095] = L'\0';
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            win_rmtree_w(child);
        else
            DeleteFileW(child);
    } while (FindNextFileW(hFind, &ffd));

    FindClose(hFind);
    RemoveDirectoryW(path);
}

void m4v_platform_remove_temp_dir(const char *dir)
{
    wchar_t* wdir = NULL;
    if (!dir || dir[0] == '\0')
        return;
    if (!m4v_utf8_to_wide(dir, &wdir))
        return;
    win_rmtree_w(wdir);
    free(wdir);
}

/* ---------------------------------------------------------------
 *  Process execution
 * --------------------------------------------------------------- */

FILE *m4v_platform_popen(const char *cmd, const char *mode)
{
    FILE*    fp;
    wchar_t* wcmd  = NULL;
    wchar_t* wmode = NULL;
    int      wlen;

    if (!cmd || !mode)
        return NULL;

    /* Convert UTF-8 strings to wide so _wpopen handles non-ANSI paths. */
    wlen = MultiByteToWideChar(CP_UTF8, 0, cmd, -1, NULL, 0);
    if (wlen <= 0) goto fallback;
    wcmd = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wcmd) goto fallback;
    MultiByteToWideChar(CP_UTF8, 0, cmd, -1, wcmd, wlen);

    wlen = MultiByteToWideChar(CP_UTF8, 0, mode, -1, NULL, 0);
    if (wlen <= 0) goto fallback;
    wmode = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wmode) goto fallback;
    MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, wlen);

    /* cmd.exe strips the outer quotes when the command line starts with a
     * quoted path (e.g. "C:\Program Files\..." args).  Wrap the whole
     * command in an extra pair of double-quotes to prevent this. */
    if (wcmd[0] == L'"') {
        size_t   cmd_wlen = wcslen(wcmd);
        wchar_t* wrapped  = (wchar_t*)malloc((cmd_wlen + 3) * sizeof(wchar_t));
        if (!wrapped) goto fallback;
        wrapped[0] = L'"';
        memcpy(wrapped + 1, wcmd, cmd_wlen * sizeof(wchar_t));
        wrapped[cmd_wlen + 1] = L'"';
        wrapped[cmd_wlen + 2] = L'\0';
        fp = _wpopen(wrapped, wmode);
        free(wrapped);
    } else {
        fp = _wpopen(wcmd, wmode);
    }

    free(wcmd);
    free(wmode);
    return fp;

fallback:
    free(wcmd);
    free(wmode);
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
