#ifndef FFMPEG_CONVERTER_SELECTION_CATALOG_H
#define FFMPEG_CONVERTER_SELECTION_CATALOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SelectionCatalog SelectionCatalog;

typedef int (*SelectionCapabilityFn)(void *context,
                                     const char *group,
                                     const char *encoder,
                                     const char *final_codec,
                                     const char *const *requires,
                                     size_t requires_count);

SelectionCatalog *selection_catalog_load(const char *catalog_path,
                                          const char *platform);
void selection_catalog_free(SelectionCatalog *catalog);

/* Returned arrays are heap allocated; strings remain owned by catalog. */
int selection_catalog_list_groups(const SelectionCatalog *catalog,
                                  SelectionCapabilityFn capability_fn,
                                  void *capability_context,
                                  const char ***out_groups);
int selection_catalog_list_encoders(const SelectionCatalog *catalog,
                                    const char *group,
                                    SelectionCapabilityFn capability_fn,
                                    void *capability_context,
                                    const char ***out_encoders);
int selection_catalog_list_presets(const SelectionCatalog *catalog,
                                   const char *group,
                                   const char *encoder,
                                   const char ***out_presets);

/* Resolve a GUI tuple to the existing converter codec ID. */
int selection_catalog_resolve(const SelectionCatalog *catalog,
                              const char *group,
                              const char *encoder,
                              char *final_codec,
                              size_t final_codec_size);

/* Frees an array returned by one of the list functions. */
void selection_catalog_free_list(const char **items);

#ifdef __cplusplus
}
#endif

#endif /* FFMPEG_CONVERTER_SELECTION_CATALOG_H */
