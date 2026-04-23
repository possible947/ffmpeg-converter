/**
 * runtime_probe_common.c
 * Platform-agnostic helper implementations shared by all runtime_probe modules.
 */

#include "runtime_probe_common.h"

#include <string.h>

void copy_string(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || dst_sz == 0)
        return;

    if (!src)
        src = "";

    strncpy(dst, src, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
}

int starts_with(const char *text, const char *prefix)
{
    size_t prefix_len;

    if (!text || !prefix)
        return 0;

    prefix_len = strlen(prefix);
    return strncmp(text, prefix, prefix_len) == 0;
}
