#ifndef FFMPEG_CONVERTER_RUNTIME_CATALOG_H
#define FFMPEG_CONVERTER_RUNTIME_CATALOG_H

int runtime_catalog_component_enabled(const char *catalog_path,
                                      const char *platform,
                                      const char *group,
                                      const char *encoder,
                                      const char *final_codec);

#endif
