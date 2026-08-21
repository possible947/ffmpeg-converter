#include "preset_loader.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <jansson.h>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
#else
    #include <unistd.h>
    #include <libgen.h>
#endif

#define MAX_PRESETS_PER_CODEC 10
#define MAX_CODECS_PER_PLATFORM 20
#define MAX_PLATFORMS 3
#define MAX_ERROR_MSG 512

static char g_error_msg[MAX_ERROR_MSG] = "No error";

static void set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_error_msg, sizeof(g_error_msg), fmt, args);
    va_end(args);
}

typedef struct {
    const char *name;
    PresetInfo preset;
} PresetEntry;

typedef struct {
    const char *name;
    PresetEntry *presets;
    size_t preset_count;
} CodecEntry;

typedef struct {
    const char *name;
    CodecEntry *codecs;
    size_t codec_count;
} PlatformEntry;

struct PresetDb {
    PlatformEntry platforms[MAX_PLATFORMS];
    size_t platform_count;
    json_t *root;
};

static const char *BUILTIN_PRESETS_JSON =
    "{"
    "\"version\":\"1.0\","
    "\"linux\":{"
    "\"copy\":{\"default\":{\"ffmpeg_args\":\"-c:v copy \",\"container\":\"mkv\"}}"
    ",\"prores\":{\"default\":{\"ffmpeg_args\":\"-c:v prores -profile:v 2 \",\"container\":\"mov\"}}"
    ",\"prores_ks\":{\"default\":{\"ffmpeg_args\":\"-c:v prores_ks -profile:v standard \",\"container\":\"mov\"}}"
    "},"
    "\"macos\":{"
    "\"copy\":{\"default\":{\"ffmpeg_args\":\"-c:v copy \",\"container\":\"mkv\"}}"
    ",\"prores\":{\"default\":{\"ffmpeg_args\":\"-c:v prores -profile:v 2 \",\"container\":\"mov\"}}"
    ",\"prores_ks\":{\"default\":{\"ffmpeg_args\":\"-c:v prores_ks -profile:v standard \",\"container\":\"mov\"}}"
    "},"
    "\"windows\":{"
    "\"copy\":{\"default\":{\"ffmpeg_args\":\"-c:v copy \",\"container\":\"mkv\"}}"
    ",\"prores\":{\"default\":{\"ffmpeg_args\":\"-c:v prores -profile:v 2 \",\"container\":\"mov\"}}"
    ",\"prores_ks\":{\"default\":{\"ffmpeg_args\":\"-c:v prores_ks -profile:v standard \",\"container\":\"mov\"}}"
    "}"
    "}";

static int file_exists(const char *path) {
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    return access(path, F_OK) == 0;
#endif
}

static char *get_executable_dir(void) {
    static char path[4096];
    
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(NULL, path, sizeof(path) - 1);
    if (len == 0) return NULL;
    path[len] = '\0';
    
    char *last_slash = strrchr(path, '\\');
    if (last_slash) {
        *last_slash = '\0';
    }
#else
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len == -1) {
        strcpy(path, ".");
    } else {
        path[len] = '\0';
        char *last_slash = strrchr(path, '/');
        if (last_slash) {
            *last_slash = '\0';
        }
    }
#endif
    
    return path;
}

static char *get_config_dir(void) {
    static char path[4096];
    
#ifdef _WIN32
    char appdata[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata) == S_OK) {
        strcpy(path, appdata);
        return path;
    }
    return NULL;
#else
    const char *home = getenv("HOME");
    if (!home) return NULL;
    
    #ifdef __APPLE__
        snprintf(path, sizeof(path), "%s/Library/Preferences", home);
    #else
        const char *xdg_config = getenv("XDG_CONFIG_HOME");
        if (xdg_config) {
            snprintf(path, sizeof(path), "%s", xdg_config);
        } else {
            snprintf(path, sizeof(path), "%s/.config", home);
        }
    #endif
    
    return path;
#endif
}

static json_t *load_presets_json(const char *search_path) {
    json_error_t error;
    json_t *root = NULL;
    
    if (search_path) {
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/presets.json", search_path);
        
        if (file_exists(full_path)) {
            root = json_load_file(full_path, 0, &error);
            if (root) {
                return root;
            }
        }
    }
    
    char *exe_dir = get_executable_dir();
    if (exe_dir) {
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/presets.json", exe_dir);
        
        if (file_exists(full_path)) {
            root = json_load_file(full_path, 0, &error);
            if (root) {
                return root;
            }
        }
    }
    
    char *config_dir = get_config_dir();
    if (config_dir) {
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/ffmpeg_converter/presets.json", config_dir);
        
        if (file_exists(full_path)) {
            root = json_load_file(full_path, 0, &error);
            if (root) {
                return root;
            }
        }
    }
    
    set_error("presets.json not found, using built-in fallback");
    root = json_loads(BUILTIN_PRESETS_JSON, 0, &error);
    
    if (!root) {
        set_error("Failed to load built-in fallback: %s", error.text);
        return NULL;
    }
    
    return root;
}

static int parse_preset_info(json_t *preset_obj, PresetInfo *info) {
    memset(info, 0, sizeof(*info));
    
    json_t *obj;
    
    obj = json_object_get(preset_obj, "ffmpeg_args");
    if (!json_is_string(obj)) {
        set_error("Preset missing ffmpeg_args");
        return -1;
    }
    info->ffmpeg_args = (char *)json_string_value(obj);
    
    obj = json_object_get(preset_obj, "container");
    if (!json_is_string(obj)) {
        set_error("Preset missing container");
        return -1;
    }
    info->container = (char *)json_string_value(obj);
    
    obj = json_object_get(preset_obj, "pix_fmt");
    if (json_is_string(obj)) {
        info->pix_fmt = (char *)json_string_value(obj);
    }
    
    obj = json_object_get(preset_obj, "pre_input_args");
    if (json_is_string(obj)) {
        info->pre_input_args = (char *)json_string_value(obj);
    }
    
    obj = json_object_get(preset_obj, "video_filter");
    if (json_is_string(obj)) {
        info->video_filter = (char *)json_string_value(obj);
    }
    
    obj = json_object_get(preset_obj, "pipeline");
    if (json_is_string(obj)) {
        info->pipeline = (char *)json_string_value(obj);
    }
    
    obj = json_object_get(preset_obj, "requires");
    if (json_is_array(obj)) {
        info->requires_count = json_array_size(obj);
    }
    
    return 0;
}

static PresetDb *build_preset_db(json_t *root) {
    PresetDb *db = calloc(1, sizeof(PresetDb));
    if (!db) {
        set_error("Memory allocation failed");
        return NULL;
    }
    
    db->root = root;
    
    const char *platform_names[] = {"linux", "macos", "windows"};
    for (int p = 0; p < 3; p++) {
        json_t *platform_obj = json_object_get(root, platform_names[p]);
        if (!platform_obj || !json_is_object(platform_obj)) {
            continue;
        }
        
        PlatformEntry *platform = &db->platforms[db->platform_count++];
        platform->name = platform_names[p];
        platform->codecs = calloc(MAX_CODECS_PER_PLATFORM, sizeof(CodecEntry));
        
        const char *codec_name;
        json_t *codec_obj;
        json_object_foreach(platform_obj, codec_name, codec_obj) {
            if (!json_is_object(codec_obj) || platform->codec_count >= MAX_CODECS_PER_PLATFORM) {
                continue;
            }
            
            CodecEntry *codec = &platform->codecs[platform->codec_count++];
            codec->name = codec_name;
            codec->presets = calloc(MAX_PRESETS_PER_CODEC, sizeof(PresetEntry));
            
            const char *preset_name;
            json_t *preset_obj;
            json_object_foreach(codec_obj, preset_name, preset_obj) {
                if (!json_is_object(preset_obj) || codec->preset_count >= MAX_PRESETS_PER_CODEC) {
                    continue;
                }
                
                PresetEntry *preset_entry = &codec->presets[codec->preset_count++];
                preset_entry->name = preset_name;
                
                if (parse_preset_info(preset_obj, &preset_entry->preset) != 0) {
                    fprintf(stderr, "Warning: Failed to parse %s/%s/%s\n",
                           platform_names[p], codec_name, preset_name);
                }
            }
        }
    }
    
    return db;
}

PresetDb *preset_db_load(const char *presets_path) {
    json_t *root = load_presets_json(presets_path);
    if (!root) {
        return NULL;
    }
    
    PresetDb *db = build_preset_db(root);
    if (!db) {
        json_decref(root);
        return NULL;
    }
    
    return db;
}

const PresetInfo *preset_db_get(PresetDb *db, const char *os_name,
                                 const char *codec_name, const char *preset_name) {
    if (!db || !os_name || !codec_name || !preset_name) {
        set_error("Invalid arguments");
        return NULL;
    }
    
    for (size_t p = 0; p < db->platform_count; p++) {
        if (strcmp(db->platforms[p].name, os_name) != 0) {
            continue;
        }
        
        for (size_t c = 0; c < db->platforms[p].codec_count; c++) {
            if (strcmp(db->platforms[p].codecs[c].name, codec_name) != 0) {
                continue;
            }
            
            for (size_t pr = 0; pr < db->platforms[p].codecs[c].preset_count; pr++) {
                if (strcmp(db->platforms[p].codecs[c].presets[pr].name, preset_name) == 0) {
                    return &db->platforms[p].codecs[c].presets[pr].preset;
                }
            }
        }
    }
    
    set_error("Preset not found: %s/%s/%s", os_name, codec_name, preset_name);
    return NULL;
}

int preset_db_list_codecs(PresetDb *db, const char *os_name,
                          const char ***out_codecs) {
    if (!db || !os_name || !out_codecs) {
        set_error("Invalid arguments");
        return -1;
    }
    
    for (size_t p = 0; p < db->platform_count; p++) {
        if (strcmp(db->platforms[p].name, os_name) == 0) {
            *out_codecs = (const char **)malloc(sizeof(char *) * db->platforms[p].codec_count);
            for (size_t c = 0; c < db->platforms[p].codec_count; c++) {
                (*out_codecs)[c] = db->platforms[p].codecs[c].name;
            }
            return (int)db->platforms[p].codec_count;
        }
    }
    
    set_error("Platform not found: %s", os_name);
    return -1;
}

int preset_db_list_presets(PresetDb *db, const char *os_name,
                           const char *codec_name, const char ***out_presets) {
    if (!db || !os_name || !codec_name || !out_presets) {
        set_error("Invalid arguments");
        return -1;
    }
    
    for (size_t p = 0; p < db->platform_count; p++) {
        if (strcmp(db->platforms[p].name, os_name) != 0) {
            continue;
        }
        
        for (size_t c = 0; c < db->platforms[p].codec_count; c++) {
            if (strcmp(db->platforms[p].codecs[c].name, codec_name) == 0) {
                *out_presets = (const char **)malloc(sizeof(char *) * db->platforms[p].codecs[c].preset_count);
                for (size_t pr = 0; pr < db->platforms[p].codecs[c].preset_count; pr++) {
                    (*out_presets)[pr] = db->platforms[p].codecs[c].presets[pr].name;
                }
                return (int)db->platforms[p].codecs[c].preset_count;
            }
        }
    }
    
    set_error("Codec not found: %s/%s", os_name, codec_name);
    return -1;
}

int preset_substitute_placeholders(char *output, size_t size,
                                   const char *template,
                                   const char *vaapi_device,
                                   int vk_device,
                                   int vt_bitrate) {
    if (!output || !template || size == 0) {
        set_error("Invalid arguments");
        return -1;
    }
    
    size_t out_idx = 0;
    size_t tmpl_idx = 0;
    
    while (template[tmpl_idx] != '\0' && out_idx < size - 1) {
        if (template[tmpl_idx] == '{') {
            if (strncmp(&template[tmpl_idx], "{vaapi_device}", 14) == 0) {
                const char *dev = vaapi_device ? vaapi_device : "/dev/dri/renderD128";
                size_t dev_len = strlen(dev);
                if (out_idx + dev_len >= size) {
                    set_error("Output buffer too small");
                    return -1;
                }
                strcpy(&output[out_idx], dev);
                out_idx += dev_len;
                tmpl_idx += 14;
            } else if (strncmp(&template[tmpl_idx], "{vk_device}", 11) == 0) {
                int device = vk_device >= 0 ? vk_device : 0;
                int written = snprintf(&output[out_idx], size - out_idx, "%d", device);
                if (written < 0 || (size_t)written >= size - out_idx) {
                    set_error("Output buffer too small");
                    return -1;
                }
                out_idx += written;
                tmpl_idx += 11;
            } else if (strncmp(&template[tmpl_idx], "{vt_bitrate}", 12) == 0) {
                int written = snprintf(&output[out_idx], size - out_idx, "%d", vt_bitrate);
                if (written < 0 || (size_t)written >= size - out_idx) {
                    set_error("Output buffer too small");
                    return -1;
                }
                out_idx += written;
                tmpl_idx += 12;
            } else {
                output[out_idx++] = template[tmpl_idx++];
            }
        } else {
            output[out_idx++] = template[tmpl_idx++];
        }
    }
    
    if (template[tmpl_idx] != '\0') {
        set_error("Output buffer too small");
        return -1;
    }
    
    output[out_idx] = '\0';
    return 0;
}

void preset_db_free(PresetDb *db) {
    if (!db) return;
    
    for (size_t p = 0; p < db->platform_count; p++) {
        for (size_t c = 0; c < db->platforms[p].codec_count; c++) {
            free(db->platforms[p].codecs[c].presets);
        }
        free(db->platforms[p].codecs);
    }
    
    if (db->root) {
        json_decref(db->root);
    }
    
    free(db);
}

const char *preset_get_last_error(void) {
    return g_error_msg;
}
