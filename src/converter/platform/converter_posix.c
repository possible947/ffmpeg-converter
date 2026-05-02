/* platform/converter_posix.c
 * POSIX (Linux + macOS) implementations of the file-system and process
 * helpers declared in converter_platform.h.
 *
 * These are 1:1 thin wrappers around the standard POSIX calls so that
 * converter.c contains no direct references to POSIX-only identifiers
 * (popen, pclose, S_ISREG, S_ISDIR) that are absent from MSVC.
 */

#include "../converter_platform.h"

#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>

int platform_stat_is_regular_file(const char *path)
{
    struct stat st;
    if (!path || path[0] == '\0') return 0;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int platform_stat_is_directory(const char *path)
{
    struct stat st;
    if (!path || path[0] == '\0') return 0;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

FILE *platform_popen(const char *cmd, const char *mode)
{
    return popen(cmd, mode);
}

int platform_pclose(FILE *fp)
{
    int rc = pclose(fp);
    if (rc == -1)
        return -1;
    if (WIFEXITED(rc))
        return WEXITSTATUS(rc);
    return -1;
}
