#include "selection_catalog.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static int hide_nvenc(void *context,
                      const char *group,
                      const char *encoder,
                      const char *final_codec,
                      const char *const *requires,
                      size_t requires_count)
{
    (void)context;
    (void)encoder;
    (void)final_codec;
    (void)requires;
    (void)requires_count;
    return strcmp(group, "nvenc") != 0;
}

static int contains(const char **items, int count, const char *value)
{
    int i;
    for (i = 0; i < count; i++) {
        if (!strcmp(items[i], value))
            return 1;
    }
    return 0;
}

int main(void)
{
    SelectionCatalog *catalog;
    const char **items = NULL;
    char codec[64];
    int count;

    catalog = selection_catalog_load(FFMPEG_CONVERTER_SOURCE_DIR "/presets.json",
                                    "linux");
    check(catalog != NULL, "load Linux selection catalog");
    if (!catalog)
        return 1;

    count = selection_catalog_list_groups(catalog, hide_nvenc, NULL, &items);
    check(contains(items, count, "software"), "list software group");
    check(contains(items, count, "mux"), "list mux group");
    check(!contains(items, count, "copy"), "copy is not a standalone group");
    check(!contains(items, count, "m4v"), "m4v is not a standalone group");
    check(!contains(items, count, "nvenc"), "capability callback hides nvenc");
    selection_catalog_free_list(items);

    count = selection_catalog_list_encoders(catalog, "mux", NULL, NULL, &items);
    check(contains(items, count, "copy"), "list mux/copy mode");
    check(contains(items, count, "mkv"), "list mux/mkv mode");
    check(contains(items, count, "m4v"), "list mux/m4v mode");
    selection_catalog_free_list(items);

    count = selection_catalog_list_encoders(catalog, "software", NULL, NULL,
                                            &items);
    check(contains(items, count, "prores"), "list software/prores encoder");
    check(contains(items, count, "prores_ks"), "list software/prores_ks encoder");
    selection_catalog_free_list(items);

    check(selection_catalog_resolve(catalog, "software", "prores_ks",
                                    codec, sizeof(codec)),
          "resolve software/prores_ks");
    check(!strcmp(codec, "prores_ks"), "software resolution preserves codec");

    count = selection_catalog_list_presets(catalog, "software", "prores",
                                           &items);
    check(contains(items, count, "lt"), "list execution presets for prores");
    check(contains(items, count, "4444"), "list all prores presets");
    selection_catalog_free_list(items);

    check(selection_catalog_resolve(catalog, "vaapi", "hevc_10bit",
                                    codec, sizeof(codec)),
          "resolve 10-bit hardware encoder");
    check(!strcmp(codec, "hevc_vaapi_10bit"),
          "10-bit resolution preserves selection variant");

    check(selection_catalog_resolve(catalog, "mux", "copy",
                                    codec, sizeof(codec)) && !strcmp(codec, "copy"),
          "resolve copy special mode");
    check(selection_catalog_resolve(catalog, "mux", "mkv",
                                    codec, sizeof(codec)) && !strcmp(codec, "mux"),
          "resolve mux container mode");
    check(selection_catalog_resolve(catalog, "mux", "m4v",
                                    codec, sizeof(codec)) && !strcmp(codec, "m4v"),
          "resolve Apple M4V mode");

    selection_catalog_free(catalog);
    if (failures)
        return 1;
    puts("Selection catalog tests passed");
    return 0;
}
