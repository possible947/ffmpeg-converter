/**
 * hq_catalog.h
 *
 * Shared HQ preset catalog provider.
 *
 * Loads the packaged HQ preset catalog (encoder_presets_v2.json) at startup
 * and serves both CLI and GUI. No hard-coded codec or preset strings.
 *
 * Responsibilities:
 *  - Load encoder_presets_v2.json from the HQ runtime root
 *  - Expose codec identifiers
 *  - Expose preset identifiers per codec
 *  - Expose default preset per codec
 *  - Preserve exact identifiers (case-sensitive)
 *
 * Does NOT:
 *  - Convert identifiers into SelectionCatalog format
 *  - Hard-code codec/preset strings
 *  - Regenerate the catalog at runtime
 */

#ifndef HQ_CATALOG_H
#define HQ_CATALOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------
 *  Catalog lifetime
 * --------------------------------------------------------------- */

/**
 * Load the HQ preset catalog from the packaged runtime tree.
 *
 * Reads encoder_presets_v2.json relative to the HQ runtime root.
 * The catalog is cached internally after the first successful load.
 *
 * @param runtime_root  Absolute path to the HQ runtime root, or NULL
 *                      to use hq_resolve_runtime_root().
 * @return 0 on success, -1 on failure.
 *
 * After a successful call, use hq_catalog_codec_count(),
 * hq_catalog_codec_name(), hq_catalog_preset_count(),
 * hq_catalog_preset_name(), and hq_catalog_default_preset()
 * to access the data.
 */
int hq_catalog_load(const char *runtime_root);

/**
 * Free the cached HQ catalog data.
 * No-op if the catalog was never loaded.
 */
void hq_catalog_free(void);

/**
 * Return 1 if the HQ catalog has been loaded and is available.
 */
int hq_catalog_is_loaded(void);

/* ---------------------------------------------------------------
 *  Codec enumeration
 * --------------------------------------------------------------- */

/** Return the number of HQ codec entries. */
size_t hq_catalog_codec_count(void);

/**
 * Return the codec identifier string at the given index.
 * The returned pointer is valid until hq_catalog_free() is called.
 * Returns NULL if index is out of range.
 */
const char *hq_catalog_codec_name(size_t index);

/* ---------------------------------------------------------------
 *  Preset enumeration per codec
 * --------------------------------------------------------------- */

/**
 * Return the number of presets for the given codec.
 * Returns 0 if the codec name is not found.
 */
size_t hq_catalog_preset_count(const char *codec);

/**
 * Return the preset identifier string at the given index for the given codec.
 * The returned pointer is valid until hq_catalog_free() is called.
 * Returns NULL if codec or index is invalid.
 */
const char *hq_catalog_preset_name(const char *codec, size_t index);

/**
 * Return the default preset for the given codec.
 * Returns NULL if the codec is not found or has no default.
 */
const char *hq_catalog_default_preset(const char *codec);

/* ---------------------------------------------------------------
 *  Validation helpers
 * --------------------------------------------------------------- */

/**
 * Return 1 if the codec exists in the HQ catalog.
 */
int hq_catalog_codec_exists(const char *codec);

/**
 * Return 1 if the preset exists for the given codec in the HQ catalog.
 */
int hq_catalog_preset_exists(const char *codec, const char *preset);

/**
 * Return 1 if the codec is an HQ ProRes encoder.
 * ProRes encoders have MOV container and PCM-only audio restrictions.
 */
int hq_catalog_is_prores_codec(const char *codec);

#ifdef __cplusplus
}
#endif

#endif /* HQ_CATALOG_H */
