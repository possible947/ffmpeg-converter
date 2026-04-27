/**
 * platform/mux_platform_windows.c
 * Windows implementation of the mux platform abstraction.
 *
 * Implements the mux_platform.h interface using:
 *  - GetFileAttributesW() for file existence checks
 *  - _wpopen() / _pclose() for process execution
 *  - Double-quote cmd.exe shell quoting
 *  - _wunlink() for file removal
 *  - MoveFileExW() for atomic rename
 *  - windows_get_preferred_*() for binary resolution
 */

#include "mux_platform.h"

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
static int mux_utf8_to_wide(const char* s, wchar_t** out) {
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

int platform_file_is_regular(const char *path)
{
    wchar_t* wpath = NULL;
    DWORD attrs;

    if (!path || path[0] == '\0')
        return 0;

    if (!mux_utf8_to_wide(path, &wpath))
        return 0;

    attrs = GetFileAttributesW(wpath);
    free(wpath);

    if (attrs == INVALID_FILE_ATTRIBUTES)
        return 0;

    /* Must not be a directory */
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

int platform_unlink(const char *path)
{
    wchar_t* wpath = NULL;
    int result;
    if (!path) return -1;
    if (!mux_utf8_to_wide(path, &wpath)) return _unlink(path);
    result = _wunlink(wpath);
    free(wpath);
    return result;
}

int platform_rename(const char *src, const char *dst)
{
    wchar_t* wsrc = NULL;
    wchar_t* wdst = NULL;
    int result;

    if (!src || !dst) return -1;
    if (!mux_utf8_to_wide(src, &wsrc)) return -1;
    if (!mux_utf8_to_wide(dst, &wdst)) { free(wsrc); return -1; }

    /* MoveFileExW with MOVEFILE_REPLACE_EXISTING atomically replaces dst
     * even if it already exists, and handles Unicode paths correctly. */
    result = MoveFileExW(wsrc, wdst, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
    free(wsrc);
    free(wdst);
    return result;
}

/* ---------------------------------------------------------------
 *  Process execution
 * --------------------------------------------------------------- */

FILE *mux_platform_popen(const char *cmd, const char *mode)
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

int platform_pclose_exitcode(FILE *fp)
{
    /* _pclose() returns the process exit code directly on Windows */
    return _pclose(fp);
}

/* ---------------------------------------------------------------
 *  Shell helpers
 * --------------------------------------------------------------- */

const char *platform_null_redirect(void)
{
    return "2>nul";
}

/**
 * platform_shell_quote() — Windows cmd.exe double-quote quoting.
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
void platform_shell_quote(const char *input, char *out, size_t out_sz)
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
