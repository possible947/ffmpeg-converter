/*  gui_codec_utils.h
 *  Shared inline codec-predicate helpers for GUI modules.
 *
 *  Both gui_window.c and gui_callbacks.c need these predicates.
 *  Defining them here as static-inline eliminates the duplicate
 *  definitions and the return-type mismatch (gboolean vs int) that
 *  existed between the two copies.
 */

#ifndef GUI_CODEC_UTILS_H
#define GUI_CODEC_UTILS_H

#include <glib.h>

static inline gboolean codec_uses_software_prores(const char *codec)
{
    return g_strcmp0(codec, "prores") == 0 ||
           g_strcmp0(codec, "prores_ks") == 0;
}

static inline gboolean codec_uses_linux_vaapi(const char *codec)
{
    return g_strcmp0(codec, "h264_vaapi") == 0 ||
           g_strcmp0(codec, "hevc_vaapi") == 0;
}

static inline gboolean codec_uses_vulkan_prores(const char *codec)
{
    return g_strcmp0(codec, "prores_ks_vulkan") == 0;
}

static inline gboolean codec_is_mux(const char *codec)
{
    return g_strcmp0(codec, "mux") == 0;
}

#endif /* GUI_CODEC_UTILS_H */
