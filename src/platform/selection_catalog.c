#include "selection_catalog.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SelectionCatalog {
    json_t *root;
    char *platform;
    json_t *selection;
    json_t *common;
    json_t *platform_groups;
};

static int enabled(json_t *value)
{
    return value && json_is_true(json_object_get(value, "enabled"));
}

static char *duplicate_string(const char *value)
{
    size_t length;
    char *copy;

    if (!value)
        return NULL;
    length = strlen(value) + 1;
    copy = malloc(length);
    if (copy)
        memcpy(copy, value, length);
    return copy;
}

static json_t *group_object(const SelectionCatalog *catalog, const char *group)
{
    json_t *object;

    if (!catalog || !group)
        return NULL;
    object = catalog->common ? json_object_get(catalog->common, group) : NULL;
    if (!object && catalog->platform_groups)
        object = json_object_get(catalog->platform_groups, group);
    return object;
}

static json_t *items_object(json_t *group, const char *group_name)
{
    if (!group || !group_name)
        return NULL;
    if (!strcmp(group_name, "mux"))
        return json_object_get(group, "modes");
    return json_object_get(group, "encoders");
}

static json_t *item_object(const SelectionCatalog *catalog,
                           const char *group,
                           const char *encoder)
{
    json_t *group_obj;
    json_t *items;

    group_obj = group_object(catalog, group);
    items = items_object(group_obj, group);
    return items ? json_object_get(items, encoder) : NULL;
}

static int append_name(const char ***items, size_t *count, const char *name)
{
    const char **grown;

    grown = realloc((void *)*items, (*count + 1) * sizeof(**items));
    if (!grown)
        return 0;
    grown[*count] = name;
    *items = grown;
    (*count)++;
    return 1;
}

static int append_group(const SelectionCatalog *catalog,
                        const char ***items,
                        size_t *count,
                        const char *group,
                        SelectionCapabilityFn capability_fn,
                        void *capability_context)
{
    const char **encoders = NULL;
    int encoder_count;

    encoder_count = selection_catalog_list_encoders(catalog, group,
                                                     capability_fn,
                                                     capability_context,
                                                     &encoders);
    selection_catalog_free_list(encoders);
    if (encoder_count <= 0)
        return 1;
    return append_name(items, count, group);
}

SelectionCatalog *selection_catalog_load(const char *catalog_path,
                                          const char *platform)
{
    SelectionCatalog *catalog;
    json_error_t error;
    json_t *platforms;
    json_t *platform_object;
    json_t *hwaccel;

    if (!catalog_path || !platform)
        return NULL;
    catalog = calloc(1, sizeof(*catalog));
    if (!catalog)
        return NULL;
    catalog->root = json_load_file(catalog_path, 0, &error);
    if (!catalog->root)
        goto fail;
    catalog->selection = json_object_get(catalog->root, "selection");
    if (!json_is_object(catalog->selection))
        goto fail;
    catalog->common = json_object_get(catalog->selection, "common");
    catalog->platform = duplicate_string(platform);
    if (!catalog->platform)
        goto fail;
    platforms = json_object_get(catalog->selection, "platforms");
    platform_object = platforms ? json_object_get(platforms, platform) : NULL;
    hwaccel = platform_object ? json_object_get(platform_object, "hwaccel") : NULL;
    catalog->platform_groups = enabled(hwaccel)
        ? json_object_get(hwaccel, "groups") : NULL;
    return catalog;

fail:
    selection_catalog_free(catalog);
    return NULL;
}

void selection_catalog_free(SelectionCatalog *catalog)
{
    if (!catalog)
        return;
    json_decref(catalog->root);
    free(catalog->platform);
    free(catalog);
}

void selection_catalog_free_list(const char **items)
{
    free((void *)items);
}

int selection_catalog_list_groups(const SelectionCatalog *catalog,
                                  SelectionCapabilityFn capability_fn,
                                  void *capability_context,
                                  const char ***out_groups)
{
    const char **groups = NULL;
    size_t count = 0;
    const char *name;
    json_t *group;

    if (!out_groups)
        return 0;
    *out_groups = NULL;
    if (!catalog)
        return 0;

    if (json_is_object(catalog->common))
    json_object_foreach(catalog->common, name, group) {
        if (!enabled(group))
            continue;
        if (!strcmp(name, "mux")) {
            append_group(catalog, &groups, &count, "mux",
                         capability_fn, capability_context);
        } else {
            append_group(catalog, &groups, &count, name,
                         capability_fn, capability_context);
        }
    }
    if (json_is_object(catalog->platform_groups))
    json_object_foreach(catalog->platform_groups, name, group) {
        if (enabled(group))
            append_group(catalog, &groups, &count, name,
                         capability_fn, capability_context);
    }
    *out_groups = groups;
    return (int)count;
}

int selection_catalog_list_encoders(const SelectionCatalog *catalog,
                                    const char *group,
                                    SelectionCapabilityFn capability_fn,
                                    void *capability_context,
                                    const char ***out_encoders)
{
    const char **encoders = NULL;
    size_t count = 0;
    json_t *group_obj;
    json_t *items;
    const char *name;
    json_t *item;

    if (!out_encoders)
        return 0;
    *out_encoders = NULL;
    if (!catalog || !group)
        return 0;

    if (!strcmp(group, "mux")) {
        append_name(&encoders, &count, "copy");
        append_name(&encoders, &count, "mkv");
        append_name(&encoders, &count, "mov");
        append_name(&encoders, &count, "m4v");
        *out_encoders = encoders;
        return (int)count;
    }

    group_obj = group_object(catalog, group);
    if (!enabled(group_obj))
        return 0;
    items = items_object(group_obj, group);
    if (!items)
        return 0;
    json_object_foreach(items, name, item) {
        json_t *final_value;
        const char *final_codec;
        json_t *requires_obj;
        const char **requires = NULL;
        size_t requires_count = 0;
        size_t i;

        if (!enabled(item))
            continue;
        final_value = json_object_get(item, "final_codec");
        if (!final_value)
            final_value = json_object_get(item, "execution_codec");
        final_codec = json_is_string(final_value) ? json_string_value(final_value) : "";
        requires_obj = json_object_get(item, "requires");
        if (json_is_array(requires_obj)) {
            json_t *requirement;
            json_array_foreach(requires_obj, i, requirement) {
                if (json_is_string(requirement))
                    append_name(&requires, &requires_count,
                                json_string_value(requirement));
            }
        }
        if (!capability_fn || capability_fn(capability_context, group, name,
                                            final_codec, requires,
                                            requires_count))
            append_name(&encoders, &count, name);
        selection_catalog_free_list(requires);
    }
    *out_encoders = encoders;
    return (int)count;
}

int selection_catalog_resolve(const SelectionCatalog *catalog,
                              const char *group,
                              const char *encoder,
                              char *final_codec,
                              size_t final_codec_size)
{
    json_t *item;
    json_t *value;
    const char *resolved;

    if (!catalog || !group || !encoder || !final_codec || final_codec_size == 0)
        return 0;
    final_codec[0] = '\0';
    if (!strcmp(group, "mux") && !strcmp(encoder, "copy"))
        resolved = "copy";
    else if (!strcmp(group, "mux") && !strcmp(encoder, "m4v"))
        resolved = "m4v";
    else if (!strcmp(group, "mux") &&
             (!strcmp(encoder, "mkv") || !strcmp(encoder, "mov")))
        resolved = "mux";
    else {
        item = item_object(catalog, group, encoder);
        if (!enabled(item))
            return 0;
        value = json_object_get(item, "final_codec");
        if (!value)
            value = json_object_get(item, "execution_codec");
        if (!json_is_string(value))
            return 0;
        resolved = json_string_value(value);
        if (strstr(encoder, "_10bit") != NULL) {
            char synthetic[128];
            snprintf(synthetic, sizeof(synthetic), "%s_10bit", resolved);
            resolved = synthetic;
            if (strlen(resolved) >= final_codec_size)
                return 0;
            strcpy(final_codec, resolved);
            return 1;
        }
    }
    if (strlen(resolved) >= final_codec_size)
        return 0;
    strcpy(final_codec, resolved);
    return 1;
}

int selection_catalog_list_presets(const SelectionCatalog *catalog,
                                   const char *group,
                                   const char *encoder,
                                   const char ***out_presets)
{
    char final_codec[128];
    json_t *item;
    json_t *preset_names;
    const char *execution_codec;
    const char **presets = NULL;
    size_t count = 0;
    size_t i;
    const char *name;
    json_t *execution_root;
    json_t *execution_codec_object;
    json_t *preset_object;

    if (!out_presets)
        return 0;
    *out_presets = NULL;
    if (!selection_catalog_resolve(catalog, group, encoder,
                                   final_codec, sizeof(final_codec)))
        return 0;
    item = item_object(catalog, group, encoder);
    preset_names = item ? json_object_get(item, "presets") : NULL;
    if (json_is_array(preset_names)) {
        json_t *preset;
        json_array_foreach(preset_names, i, preset) {
            if (json_is_string(preset))
                append_name(&presets, &count, json_string_value(preset));
        }
        *out_presets = presets;
        return (int)count;
    }

    if (!strcmp(group, "mux") && !strcmp(encoder, "copy"))
        execution_codec = "copy";
    else if (!strcmp(group, "mux") && !strcmp(encoder, "m4v"))
        execution_codec = "m4v";
    else if (!strcmp(group, "mux"))
        execution_codec = "mux";
    else
        execution_codec = final_codec;
    /* Execution sections are intentionally selected by the model's platform;
     * GUI code never needs to parse FFmpeg arguments. */
    execution_root = json_object_get(catalog->root, catalog->platform);
    execution_codec_object = execution_root
        ? json_object_get(execution_root, execution_codec) : NULL;
    if (json_is_object(execution_codec_object)) {
        json_object_foreach(execution_codec_object, name, preset_object) {
            append_name(&presets, &count, name);
        }
    }
    *out_presets = presets;
    return (int)count;
}
