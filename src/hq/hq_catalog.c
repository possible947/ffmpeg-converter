/**
 * hq_catalog.c
 *
 * HQ preset catalog provider implementation.
 *
 * Loads encoder_presets_v2.json and provides codec/preset enumeration.
 */

#include "hq_catalog.h"
#include "hq_adapter.h"

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
    const char *root;
    char catalog_path[4096];
    json_t *root_json = NULL;
    json_t *encoders = NULL;
    json_error_t error;
    FILE *fp = NULL;
    int rc = -1;

    /* If already loaded, return success */
    if (g_loaded)
        return 0;

    /* Resolve runtime root */
    if (runtime_root && runtime_root[0] != '\0') {
        root = runtime_root;
    } else {
        root = hq_resolve_runtime_root();
    }
    if (!root)
        return -1;

    /* Construct catalog path */
    snprintf(catalog_path, sizeof(catalog_path),
             "%s/modules/encoder_orchestrator_v2/config/presets/"
             "encoder_presets_v2.json",
             root);

    /* Load JSON */
    fp = fopen(catalog_path, "r");
    if (!fp)
        return -1;

    root_json = json_loadf(fp, 0, &error);
    fclose(fp);
    fp = NULL;

    if (!root_json) {
        return -1;
    }

    encoders = json_object_get(root_json, "encoders");
    if (!json_is_object(encoders)) {
        json_decref(root_json);
        return -1;
    }

    /* Count codec entries */
    {
        const char *key;
        json_t *val;
        size_t count = 0;
        json_object_foreach(encoders, key, val)
            count++;
        g_codec_count = count;
    }

    if (g_codec_count == 0) {
        json_decref(root_json);
        return -1;
    }

    /* Allocate codec entries */
    g_codecs = calloc((size_t)g_codec_count, sizeof(HqCodecEntry));
    if (!g_codecs) {
        json_decref(root_json);
        g_codec_count = 0;
        return -1;
    }

    /* Parse each encoder entry */
    {
        const char *codec_name;
        json_t *codec_obj;
        size_t idx = 0;

        json_object_foreach(encoders, codec_name, codec_obj) {
            json_t *default_preset;
            json_t *presets_obj;
            const char *preset_name;
            json_t *preset_obj;

            /* Codec name */
            snprintf(g_codecs[idx].name, sizeof(g_codecs[idx].name),
                     "%s", codec_name);

            /* Default preset */
            default_preset = json_object_get(codec_obj, "default_preset");
            if (json_is_string(default_preset)) {
                snprintf(g_codecs[idx].default_preset,
                         sizeof(g_codecs[idx].default_preset),
                         "%s",
                         json_string_value(default_preset));
            }

            /* Presets array */
            presets_obj = json_object_get(codec_obj, "presets");
            if (json_is_object(presets_obj)) {
                size_t preset_count = 0;
                size_t preset_idx = 0;

                /* Count presets */
                json_object_foreach(presets_obj, preset_name, preset_obj)
                    preset_count++;

                /* Allocate NULL-terminated array */
                g_codecs[idx].presets = calloc(preset_count + 1,
                                               sizeof(char *));
                if (g_codecs[idx].presets) {
                    json_object_foreach(presets_obj,
                                        preset_name, preset_obj) {
                        g_codecs[idx].presets[preset_idx] =
                            strdup(preset_name);
                        preset_idx++;
                    }
                    g_codecs[idx].presets[preset_idx] = NULL; /* sentinel */
                    g_codecs[idx].preset_count = preset_count;
                }
            }

            idx++;
        }
    }

    json_decref(root_json);
    g_loaded = 1;
    rc = 0;

    return rc;
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
