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

FILE *platform_popen(const char *cmd, const char *mode)
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
 * Strategy: wrap the argument in double quotes.  Inside double quotes,
 * a literal double-quote is represented as two consecutive double-quotes
 * ("").  Backslashes before a double-quote (or at end-of-string) must
 * be doubled; backslashes elsewhere are passed through unchanged.
 *
 * This matches the parsing rules of CommandLineToArgvW() and is safe
 * for use with _popen() on Windows.
 */
void platform_shell_quote(const char *input, char *out, size_t out_sz)
{
    size_t      pos = 0;
    size_t      n_bs;   /* pending backslash count */
    const char *p;

    if (!out || out_sz == 0)
        return;

    if (!input)
        input = "";

    if (out_sz < 3) {
        out[0] = '\0';
        return;
    }

    out[pos++] = '"';

    for (p = input; *p != '\0' && pos + 4 < out_sz; ++p) {
        if (*p == '\\') {
            /* Count consecutive backslashes */
            n_bs = 0;
            while (*p == '\\' && pos + (n_bs + 1) * 2 + 2 < out_sz) {
                ++n_bs;
                ++p;
            }

            if (*p == '"' || *p == '\0') {
                /* Backslashes precede a quote (or end): double them */
                size_t i;
                for (i = 0; i < n_bs * 2 && pos + 2 < out_sz; ++i)
                    out[pos++] = '\\';
            } else {
                /* Backslashes not before a quote: emit as-is */
                size_t i;
                for (i = 0; i < n_bs && pos + 2 < out_sz; ++i)
                    out[pos++] = '\\';
            }

            if (*p == '\0')
                break;

            /* Now handle the character after the backslash run */
            if (*p == '"') {
                if (pos + 3 < out_sz) {
                    out[pos++] = '"';
                    out[pos++] = '"';
                }
            } else {
                out[pos++] = *p;
            }
        } else if (*p == '"') {
            /* Escape double-quote as "" */
            if (pos + 3 < out_sz) {
                out[pos++] = '"';
                out[pos++] = '"';
            }
        } else {
            out[pos++] = *p;
        }
    }

    if (pos + 2 <= out_sz) {
        out[pos++] = '"';
        out[pos] = '\0';
    } else {
        /* Ensure null termination on truncation */
        out[out_sz - 1] = '\0';
    }
}
