/**
 * platform/mux_platform_posix.c
 * POSIX (Linux + macOS) implementation of the mux platform abstraction.
 *
 * Implements the mux_platform.h interface using:
 *  - stat() / S_ISREG for file existence checks
 *  - popen() / pclose() / WIFEXITED / WEXITSTATUS for process execution
 *  - Single-quote shell quoting (POSIX sh/bash compatible)
 *  - unlink() for file removal
 *
 * Binary resolution (platform_get_ffprobe_bin / platform_get_mkvmerge_bin)
 * is provided by the converter library via converter_platform.h and is
 * not reimplemented here.
 */

#include "mux_platform.h"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  File system
 * --------------------------------------------------------------- */

int platform_file_is_regular(const char *path)
{
    struct stat st;

    return path && path[0] != '\0' &&
           stat(path, &st) == 0 &&
           S_ISREG(st.st_mode);
}

int platform_unlink(const char *path)
{
    return unlink(path);
}

/* ---------------------------------------------------------------
 *  Process execution
 * --------------------------------------------------------------- */

FILE *platform_popen(const char *cmd, const char *mode)
{
    return popen(cmd, mode);
}

int platform_pclose_exitcode(FILE *fp)
{
    int rc = pclose(fp);
    if (rc == -1)
        return -1;
    if (WIFEXITED(rc))
        return WEXITSTATUS(rc);
    return -1;
}

/* ---------------------------------------------------------------
 *  Shell helpers
 * --------------------------------------------------------------- */

const char *platform_null_redirect(void)
{
    return "2>/dev/null";
}

void platform_shell_quote(const char *input, char *out, size_t out_sz)
{
    size_t pos = 0;

    if (!out || out_sz == 0)
        return;

    if (!input)
        input = "";

    if (out_sz < 3) {
        out[0] = '\0';
        return;
    }

    out[pos++] = '\'';
    while (*input && pos + 5 < out_sz) {
        if (*input == '\'') {
            /* End the current quote, emit escaped apostrophe, reopen */
            out[pos++] = '\'';
            out[pos++] = '\\';
            out[pos++] = '\'';
            out[pos++] = '\'';
        } else {
            out[pos++] = *input;
        }
        ++input;
    }
    out[pos++] = '\'';
    out[pos] = '\0';
}
