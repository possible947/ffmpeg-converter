/**
 * platform/mux_platform_windows.c
 * Windows implementation of the mux platform abstraction.
 *
 * Implements the mux_platform.h interface using:
 *  - GetFileAttributesA() for file existence checks
 *  - _popen() / _pclose() for process execution
 *  - Double-quote cmd.exe shell quoting
 *  - _unlink() for file removal
 *  - windows_get_preferred_*() for binary resolution
 */

#include "mux_platform.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>     /* _unlink */

/* ---------------------------------------------------------------
 *  File system
 * --------------------------------------------------------------- */

int platform_file_is_regular(const char *path)
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

int platform_unlink(const char *path)
{
    return _unlink(path);
}

/* ---------------------------------------------------------------
 *  Process execution
 * --------------------------------------------------------------- */

FILE *mux_platform_popen(const char *cmd, const char *mode)
{
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
