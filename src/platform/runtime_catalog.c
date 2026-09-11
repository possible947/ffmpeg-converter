#include "runtime_catalog.h"

#include <string.h>
#include <jansson.h>

int runtime_catalog_component_enabled(const char *catalog_path,
                                      const char *platform,
                                      const char *group,
                                      const char *encoder,
                                      const char *final_codec)
{
    json_error_t error;
    json_t *root;
    json_t *selection;
    json_t *platforms;
    json_t *platform_obj;
    json_t *hwaccel;
    json_t *groups;
    json_t *group_obj;
    json_t *encoders;
    json_t *encoder_obj;
    json_t *catalog_final_codec;
    int result = 0;

    if (!catalog_path || !platform || !group || !encoder || !final_codec)
        return 0;
    root = json_load_file(catalog_path, 0, &error);
    if (!root)
        return 0;
    selection = json_object_get(root, "selection");
    platforms = selection ? json_object_get(selection, "platforms") : NULL;
    platform_obj = platforms ? json_object_get(platforms, platform) : NULL;
    hwaccel = platform_obj ? json_object_get(platform_obj, "hwaccel") : NULL;
    groups = hwaccel ? json_object_get(hwaccel, "groups") : NULL;
    group_obj = groups ? json_object_get(groups, group) : NULL;
    if (!hwaccel || !json_is_true(json_object_get(hwaccel, "enabled")) ||
        !group_obj || !json_is_true(json_object_get(group_obj, "enabled")))
        goto done;
    encoders = json_object_get(group_obj, "encoders");
    encoder_obj = encoders ? json_object_get(encoders, encoder) : NULL;
    if (!encoder_obj || !json_is_true(json_object_get(encoder_obj, "enabled")))
        goto done;
    catalog_final_codec = json_object_get(encoder_obj, "final_codec");
    result = json_is_string(catalog_final_codec) &&
             strcmp(json_string_value(catalog_final_codec), final_codec) == 0;
done:
    json_decref(root);
    return result;
}
