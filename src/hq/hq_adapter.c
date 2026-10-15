/**
 * hq_adapter.c
 *
 * HQ Converter Library wrapper implementation.
 *
 * Process isolation model (see docs/v3.0-Phase3.1.md §5.1/§11.2 and the
 * "HQ library crash during second queue item" investigation):
 *
 * The HQ ABI's documented lifecycle is create_handle -> run_job ->
 * destroy_handle, once per job. hqclib_run_job() is executed inside a
 * short-lived forked worker process that creates its own fresh
 * hqclib_handle_t, runs exactly one job, reports the result over a pipe,
 * destroys its handle, and exits. This guarantees:
 *
 *   1. Every job gets a handle in pristine, just-after-dlopen state —
 *      no state from a previous job in the same process can leak into
 *      the next one.
 *   2. A fault inside the third-party Pascal library (segfault, abort,
 *      etc.) only terminates the disposable worker process; the caller
 *      observes a normal HQ_RESULT_FAILED instead of the whole
 *      application silently dying.
 *
 * Status callback payloads and the final result/error text are relayed
 * from the worker to the parent over a pipe using simple length-prefixed
 * frames (see HQ_FRAME_STATUS / HQ_FRAME_RESULT below). Stop requests are
 * relayed the other way over a second pipe to a watcher thread in the
 * worker, which forwards them to hqclib_request_stop() on the worker's
 * local handle.
 *
 * Re-exec, not just fork() ("GUI second-job hang/crash" investigation):
 *
 * fork()ing alone is not sufficient when the caller process is
 * multi-threaded (as the GTK GUI always is: main thread + background
 * worker thread, plus the hardware-probe thread). Between fork() and
 * _exit(), the original implementation ran non-async-signal-safe code
 * in the child — dlopen()'d library calls, malloc(), pthread_create().
 * If another thread in the parent held glibc's malloc arena lock at the
 * instant of fork(), that lock is copied into the child in a
 * permanently-locked state (the thread that owns it does not exist in
 * the child), and the very first malloc() anywhere in the child then
 * deadlocks forever. This is a classic, well-documented POSIX fork()
 * hazard; it does not affect the single-threaded CLI, and its odds grow
 * with every subsequent job in the same run — exactly matching the
 * observed "first file OK, second file hangs after encoding, before
 * mux" symptom unique to the GUI.
 *
 * The fix: the forked child does nothing but immediately execve() a
 * tiny dedicated helper binary (hq_worker, built from
 * hq_worker_main.c) before doing anything else. A freshly exec'd image
 * has its own pristine, single-threaded C runtime and malloc state, so
 * no lock can possibly be inherited in a locked state. Job parameters
 * are passed via argv and the already-open pipe fds (which survive
 * execve() since pipe() does not set FD_CLOEXEC); see
 * hq_exec_worker_or_exit() and hq_worker_main_entry() below.
 */

#include "hq_adapter.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>

#include <jansson.h>

/* environ is needed to pass the parent's environment through execve()
 * when re-executing the hq_worker helper (see hq_exec_worker_or_exit()
 * below). */
extern char **environ;

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
 *
 *  No hqclib_handle_t lives in this (parent) process any more — each
 *  job creates and destroys its own handle inside a disposable worker
 *  process (see hq_run_job()). The adapter only tracks the currently
 *  running worker, if any, so that hq_request_stop_from_converter() has
 *  something to signal, plus the last error/result text to report back.
 * --------------------------------------------------------------- */

struct HqAdapter {
    pthread_mutex_t lock;
    pid_t active_pid;          /* -1 if no job currently running */
    int   control_write_fd;    /* -1 if no job currently running */
    char  last_error[1024];
};

/* ---------------------------------------------------------------
 *  Worker <-> parent framed pipe protocol
 * --------------------------------------------------------------- */

#define HQ_FRAME_STATUS 'S'  /* payload: raw status callback JSON bytes */
#define HQ_FRAME_RESULT 'R'  /* payload: int32_t rc + UTF-8 error text  */

#define HQ_FRAME_MAX_PAYLOAD (32u * 1024u * 1024u)

static int hq_write_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

/* Reads exactly len bytes. Returns 1 on success, 0 on clean EOF before
 * any byte of this call was read, -1 on error or EOF mid-read. */
static int hq_read_all(int fd, void *buf, size_t len)
{
    char *p = (char *)buf;
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = read(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return (remaining == len) ? 0 : -1;
        p += n;
        remaining -= (size_t)n;
    }
    return 1;
}

static int hq_write_frame(int fd, char type, const void *payload, uint32_t len)
{
    char header[5];
    header[0] = type;
    memcpy(header + 1, &len, 4);
    if (hq_write_all(fd, header, sizeof(header)) != 0)
        return -1;
    if (len > 0 && payload)
        return hq_write_all(fd, payload, len);
    return 0;
}

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

    /* Set HQ runtime environment BEFORE dlopen.
     * The Pascal runtime inside the HQ library may cache environment
     * variable values at load time. Setting them afterwards is unreliable.
     *
     * HQ_PROJECT_ROOT must point to a directory whose child 'modules/'
     * exists (the HQ Library checks this). */
    setenv("HQ_PROJECT_ROOT", runtime_root, 1);

    /* The segmentator has its own path resolution that does NOT
     * use HQ_PROJECT_ROOT. Override it with its own env var. */
    {
        char seg_ini[4096];
        snprintf(seg_ini, sizeof(seg_ini),
                 "%s/modules/segmentator/config/toolchain_runtime.ini",
                 runtime_root);
        setenv("HQ_SEGMENTATOR_TOOLCHAIN_INI", seg_ini, 1);
    }

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

    if (!load_hq_library())
        return NULL;

    adapter = calloc(1, sizeof(*adapter));
    if (!adapter)
        return NULL;

    pthread_mutex_init(&adapter->lock, NULL);
    adapter->active_pid = -1;
    adapter->control_write_fd = -1;
    adapter->last_error[0] = '\0';
    return adapter;
}

void hq_adapter_destroy(HqAdapter *adapter)
{
    if (!adapter)
        return;

    pthread_mutex_lock(&adapter->lock);
    if (adapter->active_pid > 0) {
        /* hq_run_job() is synchronous from the caller's point of view,
         * so this should never happen in practice. Guard defensively
         * against a caller destroying the adapter while a worker is
         * still running instead of leaking the process. */
        kill(adapter->active_pid, SIGKILL);
        waitpid(adapter->active_pid, NULL, 0);
        adapter->active_pid = -1;
    }
    if (adapter->control_write_fd >= 0) {
        close(adapter->control_write_fd);
        adapter->control_write_fd = -1;
    }
    pthread_mutex_unlock(&adapter->lock);
    pthread_mutex_destroy(&adapter->lock);

    free(adapter);
}

void *hq_adapter_get_hqclib_handle(const HqAdapter *adapter)
{
    /* Job execution is process-isolated (see file header comment); no
     * hqclib_handle_t lives in this process any more, so there is
     * nothing meaningful to return. Kept for ABI/API compatibility. */
    (void)adapter;
    return NULL;
}

/* ===============================================================
 *  Public API — stop forwarding
 * =============================================================== */

int hq_request_stop_from_converter(HqAdapter *adapter)
{
    int rc = -1;

    if (!adapter)
        return -1;

    pthread_mutex_lock(&adapter->lock);
    if (adapter->active_pid > 0 && adapter->control_write_fd >= 0) {
        unsigned char cmd = 1;
        /* Best-effort: the worker may finish/exit concurrently, in which
         * case this write can fail (EPIPE) — that is not an error from
         * the caller's perspective, the job is simply already done. */
        (void)write(adapter->control_write_fd, &cmd, 1);
        rc = 0;
    }
    pthread_mutex_unlock(&adapter->lock);

    return rc;
}

/* ===============================================================
 *  HQ status callback bridge
 * =============================================================== */

/* Context passed through the HQ callback chain */
typedef struct {
    Converter *converter;
} HqCallbackCtx;

static void hq_status_callback_bridge(const uint8_t *json_utf8,
                                       int64_t json_length,
                                       void *user_context)
{
    HqCallbackCtx *ctx = (HqCallbackCtx *)user_context;
    const ConverterCallbacks *cb;
    json_t *root_json;
    json_t *stage_j, *progress_j, *fps_j, *eta_j, *message_j;
    const char *stage_str;
    float progress = 0, fps = 0, eta = 0;

    if (!ctx || !ctx->converter)
        return;

    cb = converter_get_callbacks(ctx->converter);
    if (!cb)
        return;

    /* Parse the JSON payload */
    root_json = json_loadb((const char *)json_utf8,
                           (size_t)json_length, 0, NULL);
    if (!root_json)
        return;

    /* Extract fields */
    stage_j = json_object_get(root_json, "stage");
    progress_j = json_object_get(root_json, "progress_percent");
    fps_j = json_object_get(root_json, "fps");
    eta_j = json_object_get(root_json, "remaining_time_sec");
    message_j = json_object_get(root_json, "message");

    stage_str = json_is_string(stage_j)
                    ? json_string_value(stage_j) : NULL;
    if (json_is_real(progress_j))
        progress = (float)json_real_value(progress_j);
    else if (json_is_integer(progress_j))
        progress = (float)(int)json_integer_value(progress_j);
    if (json_is_real(fps_j))
        fps = (float)json_real_value(fps_j);
    else if (json_is_integer(fps_j))
        fps = (float)(int)json_integer_value(fps_j);
    if (json_is_real(eta_j))
        eta = (float)json_real_value(eta_j);
    else if (json_is_integer(eta_j))
        eta = (float)(int)json_integer_value(eta_j);

    /* Map HQ stage to converter callbacks */
    if (stage_str) {
        if (strcmp(stage_str, "preparing") == 0) {
            if (cb->on_message) {
                const char *msg = json_is_string(message_j)
                                     ? json_string_value(message_j)
                                     : "Preparing HQ session...";
                cb->on_message(msg);
            }
        } else if (strcmp(stage_str, "analyzing") == 0) {
            if (cb->on_progress_analysis)
                cb->on_progress_analysis(progress, eta);
        } else if (strcmp(stage_str, "encoding") == 0) {
            if (cb->on_progress_encode)
                cb->on_progress_encode(progress, fps, eta);
        } else if (strcmp(stage_str, "completed") == 0) {
            if (cb->on_message) {
                const char *msg = json_is_string(message_j)
                                     ? json_string_value(message_j)
                                     : "HQ encoding complete";
                cb->on_message(msg);
            }
        } else if (strcmp(stage_str, "failed") == 0) {
            if (cb->on_error) {
                const char *msg = json_is_string(message_j)
                                     ? json_string_value(message_j)
                                     : "HQ processing failed";
                cb->on_error(msg, ERR_FFMPEG_FAILED);
            }
        } else if (strcmp(stage_str, "cancelled") == 0) {
            if (cb->on_error) {
                const char *msg = json_is_string(message_j)
                                     ? json_string_value(message_j)
                                     : "HQ cancelled";
                cb->on_error(msg, ERR_SKIP_FILE);
            }
        }
    }

    json_decref(root_json);
}

/* ===============================================================
 *  Worker process — one hqclib_run_job() call, fully isolated
 * =============================================================== */

/* user_context for the worker-side status callback: just the pipe fd
 * to serialize JSON payloads onto. */
typedef struct {
    int fd;
} HqChildCallbackCtx;

static void hq_child_status_callback(const uint8_t *json_utf8,
                                      int64_t json_length,
                                      void *user_context)
{
    HqChildCallbackCtx *ctx = (HqChildCallbackCtx *)user_context;
    if (!ctx || ctx->fd < 0 || json_length < 0)
        return;
    /* Best-effort; if the parent is gone there is nothing useful we can
     * do here other than let hqclib_run_job() keep running to
     * completion — the parent will observe EOF and the exit status. */
    (void)hq_write_frame(ctx->fd, HQ_FRAME_STATUS, json_utf8,
                          (uint32_t)json_length);
}

/* Watches the control pipe for a stop command and forwards it to the
 * worker's local handle. Runs for the lifetime of hqclib_run_job(). */
typedef struct {
    void *handle;
    int   control_read_fd;
} HqWatcherArgs;

static void *hq_stop_watcher_thread(void *arg)
{
    HqWatcherArgs *wa = (HqWatcherArgs *)arg;
    unsigned char cmd;

    for (;;) {
        ssize_t n = read(wa->control_read_fd, &cmd, 1);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            break; /* EOF (parent closed its end) or error */
        }
        if (g_hqclib_request_stop)
            g_hqclib_request_stop(wa->handle);
    }
    return NULL;
}

/* Runs exactly one HQ job inside the freshly forked worker process and
 * never returns — it always terminates the process via _exit(). */
static void hq_worker_run_and_exit(int status_write_fd,
                                    int control_read_fd,
                                    const char *input_path,
                                    const char *work_dir,
                                    const char *codec,
                                    const char *preset)
{
    void *handle = NULL;
    int32_t rc = 8; /* HQCLIB_RESULT_INTERNAL_ERROR, in case of early exit */
    char err_msg[1024];
    HqChildCallbackCtx cb_ctx;
    HqWatcherArgs watcher_args;
    pthread_t watcher;
    int have_watcher = 0;

    err_msg[0] = '\0';
    cb_ctx.fd = status_write_fd;

    handle = g_hqclib_create_handle ? g_hqclib_create_handle() : NULL;
    if (!handle) {
        snprintf(err_msg, sizeof(err_msg),
                 "HQ worker process failed to create a library handle");
        goto send_result;
    }

    watcher_args.handle = handle;
    watcher_args.control_read_fd = control_read_fd;
    if (pthread_create(&watcher, NULL, hq_stop_watcher_thread,
                        &watcher_args) == 0)
        have_watcher = 1;

    rc = g_hqclib_run_job(handle, input_path, work_dir, codec, preset,
                           (void *)hq_child_status_callback, &cb_ctx);

    {
        const uint8_t *msg = NULL;
        int64_t msg_len = 0;
        if (g_hqclib_get_last_error &&
            g_hqclib_get_last_error(handle, &msg, &msg_len) == 0 &&
            msg && msg_len > 0) {
            size_t copy_len = (size_t)msg_len;
            if (copy_len >= sizeof(err_msg))
                copy_len = sizeof(err_msg) - 1;
            memcpy(err_msg, msg, copy_len);
            err_msg[copy_len] = '\0';
        }
    }

send_result:
    {
        uint32_t err_len = (uint32_t)strlen(err_msg);
        size_t total = 4 + (size_t)err_len;
        char *buf = (char *)malloc(total);
        if (buf) {
            memcpy(buf, &rc, 4);
            if (err_len > 0)
                memcpy(buf + 4, err_msg, err_len);
            (void)hq_write_frame(status_write_fd, HQ_FRAME_RESULT,
                                  buf, (uint32_t)total);
            free(buf);
        }
    }

    if (handle && g_hqclib_destroy_handle)
        g_hqclib_destroy_handle(handle);

    /* Do not join the watcher thread: _exit() tears down the whole
     * process (all threads) immediately, which is fine here since the
     * watcher only ever reads a pipe and calls request_stop(). */
    (void)have_watcher;
    (void)watcher;

    close(status_write_fd);
    close(control_read_fd);
    _exit(0);
}

/* Entry point called by hq_worker_main.c's main(). This is the only
 * thing that runs in the freshly exec'd hq_worker helper process: load
 * the HQ library (this process has never called load_hq_library()
 * before — it starts with pristine zero-initialized globals, same as
 * any other fresh process image) and then run exactly one job. Never
 * returns. */
void hq_worker_main_entry(int status_write_fd,
                           int control_read_fd,
                           const char *input_path,
                           const char *work_dir,
                           const char *codec,
                           const char *preset)
{
    if (!load_hq_library()) {
        /* Report failure the same way hq_worker_run_and_exit() does,
         * so the parent's frame-reading loop in hq_run_job() handles
         * it uniformly. */
        int32_t rc = 8; /* HQCLIB_RESULT_INTERNAL_ERROR */
        static const char msg[] =
            "HQ worker process failed to load the HQ runtime library";
        char buf[4 + sizeof(msg) - 1];
        memcpy(buf, &rc, 4);
        memcpy(buf + 4, msg, sizeof(msg) - 1);
        (void)hq_write_frame(status_write_fd, HQ_FRAME_RESULT,
                              buf, (uint32_t)sizeof(buf));
        close(status_write_fd);
        close(control_read_fd);
        _exit(1);
    }

    hq_worker_run_and_exit(status_write_fd, control_read_fd,
                            input_path, work_dir, codec, preset);
    _exit(127); /* unreachable */
}

/* Re-executes this process image as the dedicated hq_worker helper
 * binary (see the file header comment for why a plain fork() is not
 * enough). Only async-signal-safe-ish operations happen here: no
 * malloc(), no dlopen(), no pthread_create() — just stack buffers,
 * readlink()/strrchr() (via resolve_exe_dir()) and execve(). Never
 * returns; calls _exit() if exec itself fails (e.g. the helper binary
 * is missing). */
static void hq_exec_worker_or_exit(int status_write_fd,
                                    int control_read_fd,
                                    const char *input_path,
                                    const char *work_dir,
                                    const char *codec,
                                    const char *preset)
{
    char exe_dir[4096];
    char worker_path[4200];
    char status_fd_buf[16];
    char control_fd_buf[16];
    char *argv[8];

    resolve_exe_dir(exe_dir, sizeof(exe_dir));
    if (exe_dir[0] == '\0')
        _exit(126);

    snprintf(worker_path, sizeof(worker_path), "%s/hq_worker", exe_dir);
    snprintf(status_fd_buf, sizeof(status_fd_buf), "%d", status_write_fd);
    snprintf(control_fd_buf, sizeof(control_fd_buf), "%d", control_read_fd);

    argv[0] = worker_path;
    argv[1] = status_fd_buf;
    argv[2] = control_fd_buf;
    argv[3] = (char *)input_path;
    argv[4] = (char *)work_dir;
    argv[5] = (char *)codec;
    argv[6] = (char *)preset;
    argv[7] = NULL;

    execve(worker_path, argv, environ);

    /* execve() only returns on failure (e.g. hq_worker missing from the
     * install — should not happen in a correctly packaged build). */
    _exit(126);
}

/* ===============================================================
 *  Public API — job execution
 * =============================================================== */

HqResult hq_run_job(HqAdapter *adapter,
                    const char *input_path,
                    const char *work_dir,
                    const char *codec,
                    const char *preset,
                    Converter *converter)
{
    const char *runtime_root;
    HqCallbackCtx cb_ctx;
    HqResult result;
    int status_pipe[2]  = { -1, -1 };
    int control_pipe[2] = { -1, -1 };
    pid_t pid;
    int32_t rc = -1;
    int have_result = 0;
    char err_text[1024];
    int wstatus = 0;

    if (!adapter || !input_path || !work_dir || !codec || !preset)
        return HQ_RESULT_INVALID_ARGUMENT;

    if (!g_hqclib_create_handle || !g_hqclib_run_job ||
        !g_hqclib_destroy_handle)
        return HQ_RESULT_LIBRARY_LOAD_FAILED;

    /* Runtime environment was set during load_hq_library (before dlopen).
     * Verify it's still valid. */
    runtime_root = hq_resolve_runtime_root();
    if (!runtime_root)
        return HQ_RESULT_RUNTIME_UNAVAILABLE;

    err_text[0] = '\0';

    if (pipe(status_pipe) != 0)
        return HQ_RESULT_INTERNAL_ERROR;
    if (pipe(control_pipe) != 0) {
        close(status_pipe[0]);
        close(status_pipe[1]);
        return HQ_RESULT_INTERNAL_ERROR;
    }

    /* Avoid duplicating buffered stdio output (e.g. CLI progress bar
     * text) into the forked worker's copy of the buffers. */
    fflush(NULL);

    pid = fork();
    if (pid < 0) {
        close(status_pipe[0]);  close(status_pipe[1]);
        close(control_pipe[0]); close(control_pipe[1]);
        return HQ_RESULT_INTERNAL_ERROR;
    }

    if (pid == 0) {
        /* Worker process — never returns. Immediately re-exec a fresh
         * process image instead of continuing to run inline (see the
         * file header comment: fork() alone is not safe when the
         * caller is multi-threaded, as the GUI always is). */
        close(status_pipe[0]);
        close(control_pipe[1]);
        hq_exec_worker_or_exit(status_pipe[1], control_pipe[0],
                                input_path, work_dir, codec, preset);
        _exit(127); /* unreachable */
    }

    /* Parent process */
    close(status_pipe[1]);
    close(control_pipe[0]);

    pthread_mutex_lock(&adapter->lock);
    adapter->active_pid = pid;
    adapter->control_write_fd = control_pipe[1];
    pthread_mutex_unlock(&adapter->lock);

    cb_ctx.converter = converter;

    for (;;) {
        char header[5];
        uint32_t len;
        char *payload = NULL;
        int r = hq_read_all(status_pipe[0], header, sizeof(header));
        if (r <= 0)
            break; /* worker exited (normally or via crash) */

        memcpy(&len, header + 1, 4);
        if (len > HQ_FRAME_MAX_PAYLOAD)
            break; /* corrupt frame — stop trusting the pipe */

        if (len > 0) {
            payload = (char *)malloc(len);
            if (!payload)
                break;
            if (hq_read_all(status_pipe[0], payload, len) <= 0) {
                free(payload);
                break;
            }
        }

        if (header[0] == HQ_FRAME_STATUS) {
            hq_status_callback_bridge((const uint8_t *)payload,
                                      (int64_t)len, &cb_ctx);
        } else if (header[0] == HQ_FRAME_RESULT && len >= 4) {
            size_t text_len = (size_t)len - 4;
            memcpy(&rc, payload, 4);
            if (text_len >= sizeof(err_text))
                text_len = sizeof(err_text) - 1;
            if (text_len > 0)
                memcpy(err_text, payload + 4, text_len);
            err_text[text_len] = '\0';
            have_result = 1;
        }

        free(payload);
    }
    close(status_pipe[0]);

    waitpid(pid, &wstatus, 0);

    pthread_mutex_lock(&adapter->lock);
    adapter->active_pid = -1;
    if (adapter->control_write_fd >= 0) {
        close(adapter->control_write_fd);
        adapter->control_write_fd = -1;
    }
    pthread_mutex_unlock(&adapter->lock);

    if (WIFSIGNALED(wstatus)) {
        int sig = WTERMSIG(wstatus);
        snprintf(adapter->last_error, sizeof(adapter->last_error),
                 "HQ worker process crashed (terminated by signal %d: %s)",
                 sig, strsignal(sig));
        return HQ_RESULT_FAILED;
    }

    if (!have_result) {
        snprintf(adapter->last_error, sizeof(adapter->last_error),
                 "HQ worker process exited unexpectedly without reporting "
                 "a result (exit status %d)",
                 WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1);
        return HQ_RESULT_FAILED;
    }

    strncpy(adapter->last_error, err_text, sizeof(adapter->last_error) - 1);
    adapter->last_error[sizeof(adapter->last_error) - 1] = '\0';

    /* Map result code */
    switch (rc) {
        case 0:  result = HQ_RESULT_OK;             break;
        case 1:  result = HQ_RESULT_CANCELLED;      break;
        case 2:  result = HQ_RESULT_FAILED;         break;
        case 3:  result = HQ_RESULT_INVALID_ARGUMENT; break;
        case 4:  result = HQ_RESULT_INVALID_UTF8;   break;
        case 5:  result = HQ_RESULT_INVALID_STATE;  break;
        case 6:  result = HQ_RESULT_IO_ERROR;       break;
        case 7:  result = HQ_RESULT_CONTROL_ERROR;  break;
        case 8:  result = HQ_RESULT_INTERNAL_ERROR; break;
        case 9:  result = HQ_RESULT_NOT_RUNNING;    break;
        default: result = HQ_RESULT_INTERNAL_ERROR; break;
    }

    return result;
}

/* ===============================================================
 *  Public API — last error
 * =============================================================== */

int hq_adapter_get_last_error(HqAdapter *adapter,
                               char *buf, size_t buf_sz)
{
    if (!adapter) {
        if (buf && buf_sz > 0)
            buf[0] = '\0';
        return -1;
    }

    if (buf && buf_sz > 0) {
        strncpy(buf, adapter->last_error, buf_sz - 1);
        buf[buf_sz - 1] = '\0';
        return 0;
    }

    return -1;
}

/* ===============================================================
 *  Public API — eov2 session reader
 * =============================================================== */

int hq_read_video_track_path(const char *work_dir,
                              const char *codec,
                              const char *preset,
                              char *out_path,
                              size_t out_path_sz)
{
    char session_path[4096];
    json_t *root_json = NULL;
    json_t *artifacts = NULL;
    json_t *outputs = NULL;
    json_t *video_track = NULL;
    json_t *path_json = NULL;
    json_error_t error;
    FILE *fp = NULL;
    int rc = -1;

    if (!work_dir || !codec || !preset || !out_path || out_path_sz == 0)
        return -1;

    out_path[0] = '\0';

    /* Construct session file path */
    snprintf(session_path, sizeof(session_path),
             "%s/job.%s.%s.eov2.session.json",
             work_dir, codec, preset);

    /* Load JSON */
    fp = fopen(session_path, "r");
    if (!fp)
        return -1;

    root_json = json_loadf(fp, 0, &error);
    fclose(fp);
    fp = NULL;

    if (!root_json)
        return -1;

    /* Navigate: artifacts -> outputs -> video_track -> path */
    artifacts = json_object_get(root_json, "artifacts");
    if (!json_is_object(artifacts))
        goto done;

    outputs = json_object_get(artifacts, "outputs");
    if (!json_is_object(outputs))
        goto done;

    video_track = json_object_get(outputs, "video_track");
    if (!json_is_object(video_track))
        goto done;

    path_json = json_object_get(video_track, "path");
    if (!json_is_string(path_json))
        goto done;

    {
        const char *path_str = json_string_value(path_json);
        if (path_str) {
            strncpy(out_path, path_str, out_path_sz - 1);
            out_path[out_path_sz - 1] = '\0';
            rc = 0;
        }
    }

done:
    json_decref(root_json);
    return rc;
}

/* ===============================================================
 *  Recursive directory removal (task_N cleanup)
 * =============================================================== */

static int hq_rmtree_recursive(const char *path)
{
    DIR *d;
    struct dirent *ent;
    struct stat st;
    char child[2048];

    if (!path || path[0] == '\0')
        return -1;

    /* Stat the path; if it doesn't exist, nothing to do */
    if (stat(path, &st) != 0) {
        if (errno == ENOENT)
            return 0;
        return -1;
    }

    /* If it's a directory, recurse into it */
    if (S_ISDIR(st.st_mode)) {
        d = opendir(path);
        if (!d)
            return -1;

        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 ||
                strcmp(ent->d_name, "..") == 0)
                continue;

            snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            if (hq_rmtree_recursive(child) != 0) {
                closedir(d);
                return -1;
            }
        }

        closedir(d);

        /* Remove the directory itself */
        if (rmdir(path) != 0)
            return -1;
    } else {
        /* Regular file or symlink — unlink */
        if (unlink(path) != 0)
            return -1;
    }

    return 0;
}

int hq_remove_directory_tree(const char *path)
{
    return hq_rmtree_recursive(path);
}
