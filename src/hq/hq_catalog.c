/**
 * hq_catalog.c
 *
 * HQ preset catalog provider implementation.
 *
 * Loads encoder_presets_v2.json and provides codec/preset enumeration.
 *
 * Phase 1 commit: stub functions only.
 * Full implementation will be added in subsequent commits.
 */

#include "hq_catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

/* ===============================================================
 *  Internal catalog cache
 * =============================================================== */

/* Single codec entry */
typedef struct {
    char name[64];
    char default_preset[64];
    char **presets;    /* NULL-terminated array of preset names */
    size_t preset_count;
} HqCodecEntry;

static HqCodecEntry *g_codecs = NULL;
static size_t g_codec_count = 0;
static int g_loaded = 0;

/* ===============================================================
 *  Public API — lifecycle
 * =============================================================== */

int hq_catalog_load(const char *runtime_root)
{
    (void)runtime_root;
    /* Full implementation in Phase 2 commit */
    g_loaded = 0;
    return -1;
}

void hq_catalog_free(void)
{
    size_t i;

    if (g_codecs) {
        for (i = 0; i < g_codec_count; i++) {
            if (g_codecs[i].presets) {
                size_t j;
                for (j = 0; g_codecs[i].presets[j]; j++) {
                    free(g_codecs[i].presets[j]);
                }
                free(g_codecs[i].presets);
                g_codecs[i].presets = NULL;
            }
        }
        free(g_codecs);
        g_codecs = NULL;
    }

    g_codec_count = 0;
    g_loaded = 0;
}

int hq_catalog_is_loaded(void)
{
    return g_loaded;
}

/* ===============================================================
 *  Public API — codec enumeration
 * =============================================================== */

size_t hq_catalog_codec_count(void)
{
    return g_codec_count;
}

const char *hq_catalog_codec_name(size_t index)
{
    if (index >= g_codec_count)
        return NULL;
    return g_codecs[index].name;
}

/* ===============================================================
 *  Public API — preset enumeration
 * =============================================================== */

static const HqCodecEntry *find_codec(const char *codec)
{
    size_t i;
    if (!codec)
        return NULL;
    for (i = 0; i < g_codec_count; i++) {
        if (strcmp(g_codecs[i].name, codec) == 0)
            return &g_codecs[i];
    }
    return NULL;
}

size_t hq_catalog_preset_count(const char *codec)
{
    const HqCodecEntry *entry = find_codec(codec);
    if (!entry)
        return 0;
    return entry->preset_count;
}

const char *hq_catalog_preset_name(const char *codec, size_t index)
{
    const HqCodecEntry *entry = find_codec(codec);
    if (!entry || !entry->presets || index >= entry->preset_count)
        return NULL;
    return entry->presets[index];
}

const char *hq_catalog_default_preset(const char *codec)
{
    const HqCodecEntry *entry = find_codec(codec);
    if (!entry || entry->default_preset[0] == '\0')
        return NULL;
    return entry->default_preset;
}

/* ===============================================================
 *  Public API — validation
 * =============================================================== */

int hq_catalog_codec_exists(const char *codec)
{
    return find_codec(codec) != NULL;
}

int hq_catalog_preset_exists(const char *codec, const char *preset)
{
    const HqCodecEntry *entry = find_codec(codec);
    if (!entry || !preset)
        return 0;
    for (size_t i = 0; i < entry->preset_count; i++) {
        if (strcmp(entry->presets[i], preset) == 0)
            return 1;
    }
    return 0;
}

int hq_catalog_is_prores_codec(const char *codec)
{
    return codec &&
           (strcmp(codec, "ffmpeg-prores") == 0 ||
            strcmp(codec, "ffmpeg-prores_ks") == 0);
}
