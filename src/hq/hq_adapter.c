/**
 * hq_adapter.c
 *
 * HQ Converter Library wrapper implementation.
 *
 * Phase 1 commit: stub functions only.
 * Full implementation will be added in subsequent commits.
 */

#include "hq_adapter.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <jansson.h>

/* ---------------------------------------------------------------
 *  Internal ABI function pointers
 * --------------------------------------------------------------- */

typedef void *(*fn_hqclib_create_handle)(void);
typedef void  (*fn_hqclib_destroy_handle)(void *handle);
typedef int32_t (*fn_hqclib_run_job)(
    void *handle,
    const char *input_file_utf8,
    const char *work_dir_utf8,
    const char *codec_utf8,
    const char *preset_utf8,
    void *callback,
    void *user_context);
typedef int32_t (*fn_hqclib_request_stop)(void *handle);
typedef int32_t (*fn_hqclib_request_kill)(void *handle);
typedef int32_t (*fn_hqclib_get_last_error)(
    void *handle,
    const uint8_t **message_utf8,
    int64_t *message_length);

/* ---------------------------------------------------------------
 *  Library handle and symbols (process-wide lazy init)
 * --------------------------------------------------------------- */

static void *g_hq_lib_handle = NULL;
static fn_hqclib_create_handle   g_hqclib_create_handle   = NULL;
static fn_hqclib_destroy_handle  g_hqclib_destroy_handle  = NULL;
static fn_hqclib_run_job         g_hqclib_run_job         = NULL;
static fn_hqclib_request_stop    g_hqclib_request_stop    = NULL;
static fn_hqclib_request_kill    g_hqclib_request_kill    = NULL;
static fn_hqclib_get_last_error  g_hqclib_get_last_error  = NULL;

/* ---------------------------------------------------------------
 *  Runtime root (cached)
 * --------------------------------------------------------------- */

static char g_hq_runtime_root[4096];
static int  g_runtime_root_resolved = 0;

/* ---------------------------------------------------------------
 *  Adapter context
 * --------------------------------------------------------------- */

struct HqAdapter {
    void *hq_handle;   /* opaque hqclib_handle_t */
};

/* ===============================================================
 *  Runtime root resolution
 * =============================================================== */

static void resolve_exe_dir(char *buf, size_t buf_sz)
{
    buf[0] = '\0';

#if defined(__APPLE__)
    {
        uint32_t size = (uint32_t)buf_sz;
        if (_NSGetExecutablePath(buf, &size) != 0)
            return; /* buffer too small */
    }
#else
    {
        ssize_t len = readlink("/proc/self/exe", buf, (size_t)buf_sz - 1);
        if (len <= 0)
            return;
        buf[len] = '\0';
    }
#endif

    /* Strip filename to get directory */
    char *slash = strrchr(buf, '/');
    if (slash)
        *slash = '\0';
}

const char *hq_resolve_runtime_root(void)
{
    if (g_runtime_root_resolved)
        return g_hq_runtime_root[0] != '\0' ? g_hq_runtime_root : NULL;

    g_runtime_root_resolved = 1;
    g_hq_runtime_root[0] = '\0';

    /* 1. Development override */
    const char *env = getenv("FFC_HQ_ROOT");
    if (env && env[0] != '\0') {
        strncpy(g_hq_runtime_root, env, sizeof(g_hq_runtime_root) - 1);
        g_hq_runtime_root[sizeof(g_hq_runtime_root) - 1] = '\0';
        /* Validate: libhq_converter.so must exist at expected path */
        char lib_path[4096];
#if defined(__APPLE__)
        snprintf(lib_path, sizeof(lib_path),
                 "%s/modules/library/bin/libhq_converter.dylib",
                 g_hq_runtime_root);
#else
        snprintf(lib_path, sizeof(lib_path),
                 "%s/modules/library/bin/libhq_converter.so",
                 g_hq_runtime_root);
#endif
        if (access(lib_path, F_OK) == 0)
            return g_hq_runtime_root;
        /* Override exists but is invalid — clear it */
        g_hq_runtime_root[0] = '\0';
        return NULL;
    }

    /* 2. Executable-relative resolution */
    char exe_dir[4096];
    resolve_exe_dir(exe_dir, sizeof(exe_dir));
    if (exe_dir[0] == '\0')
        return NULL;

    /* Append /hq_converter */
    size_t base_len = strlen(exe_dir);
    if (base_len + 15 >= sizeof(g_hq_runtime_root))
        return NULL;
    strncpy(g_hq_runtime_root, exe_dir, sizeof(g_hq_runtime_root) - 1);
    g_hq_runtime_root[sizeof(g_hq_runtime_root) - 1] = '\0';
    strncat(g_hq_runtime_root, "/hq_converter",
            sizeof(g_hq_runtime_root) - base_len - 1);

    /* 3. macOS .app bundle: check Contents/Resources/hq_converter */
#if defined(__APPLE__)
    {
        char bundle_path[4096];
        snprintf(bundle_path, sizeof(bundle_path),
                 "%s/../Resources/hq_converter", exe_dir);
        char bundle_lib[4096];
        snprintf(bundle_lib, sizeof(bundle_lib),
                 "%s/modules/library/bin/libhq_converter.dylib",
                 bundle_path);
        if (access(bundle_lib, F_OK) == 0) {
            strncpy(g_hq_runtime_root, bundle_path,
                    sizeof(g_hq_runtime_root) - 1);
            g_hq_runtime_root[sizeof(g_hq_runtime_root) - 1] = '\0';
            return g_hq_runtime_root;
        }
    }
#endif

    /* 4. Validate: libhq_converter.so must exist at resolved path */
    char lib_path[4096];
#if defined(__APPLE__)
    snprintf(lib_path, sizeof(lib_path),
             "%s/modules/library/bin/libhq_converter.dylib",
             g_hq_runtime_root);
#else
    snprintf(lib_path, sizeof(lib_path),
             "%s/modules/library/bin/libhq_converter.so",
             g_hq_runtime_root);
#endif
    if (access(lib_path, F_OK) != 0) {
        g_hq_runtime_root[0] = '\0';
        return NULL;
    }

    return g_hq_runtime_root;
}

/* ===============================================================
 *  Library loading and symbol resolution
 * =============================================================== */

static int load_hq_library(void)
{
    const char *runtime_root;

    if (g_hq_lib_handle)
        return 1; /* already loaded */

    runtime_root = hq_resolve_runtime_root();
    if (!runtime_root)
        return 0;

    char lib_path[4096];
#if defined(__APPLE__)
    snprintf(lib_path, sizeof(lib_path),
             "%s/modules/library/bin/libhq_converter.dylib",
             runtime_root);
#else
    snprintf(lib_path, sizeof(lib_path),
             "%s/modules/library/bin/libhq_converter.so",
             runtime_root);
#endif

    g_hq_lib_handle = dlopen(lib_path, RTLD_NOW);
    if (!g_hq_lib_handle) {
        return 0;
    }

    /* Resolve required public ABI symbols */
    g_hqclib_create_handle  = (fn_hqclib_create_handle)
        dlsym(g_hq_lib_handle, "hqclib_create_handle");
    g_hqclib_destroy_handle = (fn_hqclib_destroy_handle)
        dlsym(g_hq_lib_handle, "hqclib_destroy_handle");
    g_hqclib_run_job        = (fn_hqclib_run_job)
        dlsym(g_hq_lib_handle, "hqclib_run_job");
    g_hqclib_request_stop   = (fn_hqclib_request_stop)
        dlsym(g_hq_lib_handle, "hqclib_request_stop");
    g_hqclib_request_kill   = (fn_hqclib_request_kill)
        dlsym(g_hq_lib_handle, "hqclib_request_kill");
    g_hqclib_get_last_error = (fn_hqclib_get_last_error)
        dlsym(g_hq_lib_handle, "hqclib_get_last_error");

    /* All symbols are required */
    if (!g_hqclib_create_handle ||
        !g_hqclib_destroy_handle ||
        !g_hqclib_run_job ||
        !g_hqclib_request_stop ||
        !g_hqclib_request_kill ||
        !g_hqclib_get_last_error) {
        dlclose(g_hq_lib_handle);
        g_hq_lib_handle = NULL;
        return 0;
    }

    return 1;
}

/* ===============================================================
 *  Public API — availability
 * =============================================================== */

int hq_is_available(void)
{
    return load_hq_library() ? 1 : 0;
}

/* ===============================================================
 *  Public API — adapter lifecycle
 * =============================================================== */

HqAdapter *hq_adapter_create(void)
{
    HqAdapter *adapter;
    void *hq_handle;

    if (!load_hq_library())
        return NULL;

    adapter = calloc(1, sizeof(*adapter));
    if (!adapter)
        return NULL;

    hq_handle = g_hqclib_create_handle();
    if (!hq_handle) {
        free(adapter);
        return NULL;
    }

    adapter->hq_handle = hq_handle;
    return adapter;
}

void hq_adapter_destroy(HqAdapter *adapter)
{
    if (!adapter)
        return;

    if (adapter->hq_handle && g_hqclib_destroy_handle) {
        g_hqclib_destroy_handle(adapter->hq_handle);
        adapter->hq_handle = NULL;
    }

    free(adapter);
}

void *hq_adapter_get_hqclib_handle(const HqAdapter *adapter)
{
    if (!adapter)
        return NULL;
    return adapter->hq_handle;
}

/* ===============================================================
 *  Public API — stop forwarding
 * =============================================================== */

int hq_request_stop_from_converter(HqAdapter *adapter)
{
    if (!adapter || !adapter->hq_handle || !g_hqclib_request_stop)
        return -1;

    g_hqclib_request_stop(adapter->hq_handle);
    return 0;
}

/* ===============================================================
 *  Public API — job execution (stub)
 * =============================================================== */

HqResult hq_run_job(HqAdapter *adapter,
                    const char *input_path,
                    const char *work_dir,
                    const char *codec,
                    const char *preset,
                    Converter *converter)
{
    (void)adapter;
    (void)input_path;
    (void)work_dir;
    (void)codec;
    (void)preset;
    (void)converter;
    /* Full implementation in Phase 2 commit */
    return HQ_RESULT_FAILED;
}

/* ===============================================================
 *  Public API — last error
 * =============================================================== */

int hq_adapter_get_last_error(HqAdapter *adapter,
                               char *buf, size_t buf_sz)
{
    const uint8_t *msg = NULL;
    int64_t msg_len = 0;

    if (!adapter || !adapter->hq_handle || !g_hqclib_get_last_error) {
        if (buf && buf_sz > 0)
            buf[0] = '\0';
        return -1;
    }

    g_hqclib_get_last_error(adapter->hq_handle, &msg, &msg_len);

    if (buf && buf_sz > 0) {
        size_t copy_len = (size_t)msg_len;
        if (copy_len >= buf_sz)
            copy_len = buf_sz - 1;
        if (msg)
            memcpy(buf, msg, copy_len);
        buf[copy_len] = '\0';
        return 0;
    }

    return -1;
}

/* ===============================================================
 *  Public API — eov2 session reader (stub)
 * =============================================================== */

int hq_read_video_track_path(const char *work_dir,
                              const char *codec,
                              const char *preset,
                              char *out_path,
                              size_t out_path_sz)
{
    (void)work_dir;
    (void)codec;
    (void)preset;
    if (out_path && out_path_sz > 0)
        out_path[0] = '\0';
    /* Full implementation in Phase 2 commit */
    return -1;
}
