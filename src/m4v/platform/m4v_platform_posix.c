/**
 * platform/m4v_platform_posix.c
 * POSIX (Linux + macOS) implementation of the m4v platform abstraction.
 *
 * Implements the m4v_platform.h interface using:
 *  - stat() / S_ISREG / access() for file existence checks
 *  - mkdtemp() for temporary directory creation
 *  - Recursive opendir/unlink/rmdir for directory removal
 *  - popen() / pclose() / WIFEXITED / WEXITSTATUS for process execution
 *  - Single-quote shell quoting (POSIX sh/bash compatible)
 *  - unlink() for file removal
 *
 * Binary resolution (platform_get_ffmpeg_bin / platform_get_ffprobe_bin /
 * platform_get_mp4box_bin) is provided by the converter library via
 * converter_platform.h and is not reimplemented here.
 */

#include "../m4v_platform.h"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---------------------------------------------------------------
 *  File system
 * --------------------------------------------------------------- */

int m4v_platform_is_regular_file(const char *path)
{
    struct stat st;

    return path && path[0] != '\0' &&
           stat(path, &st) == 0 &&
           S_ISREG(st.st_mode) &&
           access(path, R_OK) == 0;
}

int m4v_platform_file_exists(const char *path)
{
    return path && path[0] != '\0' && access(path, F_OK) == 0;
}

int m4v_platform_unlink(const char *path)
{
    return unlink(path);
}

/* ---------------------------------------------------------------
 *  Temp directory management
 * --------------------------------------------------------------- */

int m4v_platform_make_temp_dir(char *path, size_t path_sz)
{
    char templ[1024];
    char *made;

    if (!path || path_sz == 0)
        return 0;

    snprintf(templ, sizeof(templ), "/tmp/m4v_mux_XXXXXX");
    made = mkdtemp(templ);
    if (!made)
        return 0;

    if (strlen(made) + 1 > path_sz)
        return 0;

    strncpy(path, made, path_sz - 1);
    path[path_sz - 1] = '\0';
    return 1;
}

/* Recursive helper — removes all contents of dir, then dir itself.
 * Uses opendir() instead of stat() to avoid TOCTOU race conditions. */
static void posix_rmtree(const char *path)
{
    DIR *d;
    struct dirent *ent;
    char child[2048];

    if (!path || path[0] == '\0')
        return;

    /* Attempt to open as directory; if it is not a directory, treat as file */
    d = opendir(path);
    if (!d) {
        unlink(path);
        return;
    }

    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        posix_rmtree(child);
    }
    closedir(d);
    rmdir(path);
}

void m4v_platform_remove_temp_dir(const char *dir)
{
    if (!dir || dir[0] == '\0')
        return;
    posix_rmtree(dir);
}

/* ---------------------------------------------------------------
 *  Process execution
 * --------------------------------------------------------------- */

FILE *m4v_platform_popen(const char *cmd, const char *mode)
{
    return popen(cmd, mode);
}

int m4v_platform_pclose_exitcode(FILE *fp)
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

const char *m4v_platform_null_redirect(void)
{
    return "2>/dev/null";
}

void m4v_platform_shell_quote(const char *input, char *out, size_t out_sz)
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
    while (*input) {
        if (*input == '\'') {
            /* Need 4 bytes for escape sequence + closing quote + NUL = 6 bytes */
            if (pos + 6 > out_sz)
                break;
            out[pos++] = '\'';
            out[pos++] = '\\';
            out[pos++] = '\'';
            out[pos++] = '\'';
        } else {
            /* Need 1 byte for char + closing quote + NUL = 3 bytes */
            if (pos + 3 > out_sz)
                break;
            out[pos++] = *input;
        }
        ++input;
    }
    /* Write closing quote if room */
    if (pos < out_sz - 1)
        out[pos++] = '\'';
    out[pos < out_sz ? pos : out_sz - 1] = '\0';
}
