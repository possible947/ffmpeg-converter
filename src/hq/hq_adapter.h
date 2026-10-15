/**
 * hq_adapter.h
 *
 * Thin wrapper around the HQ Converter Library public ABI.
 *
 * Responsibilities:
 *  - Resolve the packaged HQ runtime root
 *  - Set HQ_PROJECT_ROOT environment variable
 *  - Dynamically load libhq_converter.so / .dylib
 *  - Resolve public ABI symbols
 *  - Manage HQ handle lifecycle
 *  - Execute synchronous HQ jobs with status callback forwarding
 *  - Read eov2 result session for video_track.path
 *  - Forward Stop/Kill through public library control API
 *
 * Does NOT own:
 *  - Audio processing (uses existing Audio Orchestrator)
 *  - Mux processing (uses existing mux module)
 *  - Output path logic (uses existing converter output workflow)
 *
 * Process isolation:
 *  - hq_run_job() executes hqclib_run_job() inside a short-lived forked
 *    + re-exec'd worker process (the hq_worker helper binary) with its
 *    own fresh hqclib_handle_t, created and destroyed solely within that
 *    worker. No hqclib_handle_t survives across calls to hq_run_job() in
 *    this (parent) process. This both matches the documented per-job
 *    create/run/destroy lifecycle and guarantees that a fault inside
 *    the third-party library only ends the disposable worker process,
 *    not the caller.
 *  - The worker re-execs immediately after fork(), before doing any
 *    non-async-signal-safe work (malloc, dlopen, pthread_create). This
 *    is required, not optional: fork()ing from a multi-threaded caller
 *    (e.g. the GTK GUI) without an immediate exec() risks inheriting a
 *    malloc arena lock in a permanently-locked state if another thread
 *    held it at fork time, deadlocking the worker. See hq_adapter.c's
 *    file header for the full rationale.
 *  - Status callback payloads are relayed from the worker to the
 *    caller's Converter callbacks over a pipe, synchronously, as part
 *    of hq_run_job()'s blocking read loop.
 *
 * Thread model:
 *  - hq_resolve_runtime_root() is safe to call from any thread
 *    (lazy init, read-only after first call).
 *  - hq_run_job() blocks the calling thread until the worker process
 *    exits.
 *  - hq_request_stop_from_converter() may be called from another
 *    thread while hq_run_job() is blocked; it signals the active
 *    worker process over a control pipe.
 */

#ifndef HQ_ADAPTER_H
#define HQ_ADAPTER_H

#include <stddef.h>
#include <stdint.h>
#include "converter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------
 *  Result codes (mirror HQCLIB public enum)
 * --------------------------------------------------------------- */

typedef enum {
    HQ_RESULT_OK = 0,
    HQ_RESULT_CANCELLED,
    HQ_RESULT_FAILED,
    HQ_RESULT_INVALID_ARGUMENT,
    HQ_RESULT_INVALID_UTF8,
    HQ_RESULT_INVALID_STATE,
    HQ_RESULT_IO_ERROR,
    HQ_RESULT_CONTROL_ERROR,
    HQ_RESULT_INTERNAL_ERROR,
    HQ_RESULT_NOT_RUNNING,
    /* Internal — not from the HQ library itself */
    HQ_RESULT_RUNTIME_UNAVAILABLE,
    HQ_RESULT_LIBRARY_LOAD_FAILED,
    HQ_RESULT_SESSION_READ_FAILED
} HqResult;

/* ---------------------------------------------------------------
 *  Runtime root resolution
 * --------------------------------------------------------------- */

/**
 * Resolve the absolute HQ runtime root path.
 *
 * Linux (local build, installed, AppImage):
 *   <executable_dir>/hq_converter/
 *
 * macOS (.app bundle):
 *   <App>.app/Contents/Resources/hq_converter/
 *
 * macOS (standalone CLI):
 *   <executable_dir>/hq_converter/
 *
 * Returns a null-terminated string valid for the lifetime of the process.
 * Returns NULL if the runtime root cannot be resolved.
 */
const char *hq_resolve_runtime_root(void);

/* ---------------------------------------------------------------
 *  HQ availability check
 * --------------------------------------------------------------- */

/**
 * Return 1 if the HQ runtime is available (runtime root resolved and
 * library loaded), 0 otherwise.
 */
int hq_is_available(void);

/* ---------------------------------------------------------------
 *  HQ handle management
 * --------------------------------------------------------------- */

/**
 * Opaque HQ adapter context. Holds the library handle and a pointer
 * back to the Converter so that Stop can be forwarded.
 *
 * Caller creates and destroys; NOT shared across threads.
 */
typedef struct HqAdapter HqAdapter;

/**
 * Create an HQ adapter context.
 * Returns NULL on failure (e.g., HQ runtime unavailable).
 */
HqAdapter *hq_adapter_create(void);

/**
 * Destroy the HQ adapter context and release the HQ handle.
 * Safe to call with NULL.
 */
void hq_adapter_destroy(HqAdapter *adapter);

/**
 * Deprecated / always returns NULL.
 *
 * Job execution is process-isolated (see the file header comment in
 * hq_adapter.c): the hqclib_handle_t is created and destroyed entirely
 * inside a disposable worker process and never exists in the caller's
 * process. Kept for API compatibility.
 */
void *hq_adapter_get_hqclib_handle(const HqAdapter *adapter);

/* ---------------------------------------------------------------
 *  Stop forwarding
 * --------------------------------------------------------------- */

/**
 * Forward converter_stop() to the currently running HQ worker process,
 * which relays it to hqclib_request_stop() on its local handle.
 * Returns 0 on success (a worker was active and signalled), -1 if no
 * job is currently running.
 */
int hq_request_stop_from_converter(HqAdapter *adapter);

/* ---------------------------------------------------------------
 *  Job execution
 * --------------------------------------------------------------- */

/**
 * Run a synchronous HQ Converter job.
 *
 * Internally forks a disposable worker process that creates its own
 * fresh hqclib_handle_t, runs the job, and exits. A crash inside the
 * third-party library is contained to that worker and reported as
 * HQ_RESULT_FAILED (with a descriptive message retrievable via
 * hq_adapter_get_last_error()) instead of terminating the caller.
 *
 * @param adapter       HqAdapter context (tracks the active worker, if any).
 * @param input_path    Absolute path to the input file.
 * @param work_dir      Absolute path to the task_N working directory.
 * @param codec         HQ codec identifier from the HQ catalog.
 * @param preset        HQ preset identifier from the HQ catalog.
 * @param converter     Caller-owned Converter for callback forwarding.
 * @return HqResult code.
 */
HqResult hq_run_job(
    HqAdapter *adapter,
    const char *input_path,
    const char *work_dir,
    const char *codec,
    const char *preset,
    Converter *converter
);

/* ---------------------------------------------------------------
 *  Worker helper entry point (internal — used only by hq_worker_main.c)
 * --------------------------------------------------------------- */

/**
 * Entry point for the dedicated hq_worker helper process (see the
 * process-isolation / re-exec rationale in hq_adapter.c's file header
 * comment). Loads the HQ library in this fresh process image and runs
 * exactly one job, reporting status/result over the given pipe fds.
 *
 * Not part of the public HqAdapter API — only hq_worker_main.c's
 * main() calls this, immediately after exec(), before doing anything
 * else. Never returns; always terminates the process.
 *
 * @param status_write_fd  Write end of the status/result pipe to the parent.
 * @param control_read_fd  Read end of the stop-control pipe from the parent.
 * @param input_path       Absolute path to the input file.
 * @param work_dir          Absolute path to the task_N working directory.
 * @param codec             HQ codec identifier from the HQ catalog.
 * @param preset            HQ preset identifier from the HQ catalog.
 */
void hq_worker_main_entry(int status_write_fd,
                           int control_read_fd,
                           const char *input_path,
                           const char *work_dir,
                           const char *codec,
                           const char *preset);

/* ---------------------------------------------------------------
 *  Error text
 * --------------------------------------------------------------- */

/**
 * Retrieve the last error message from the HQ library.
 * Writes up to (buf_sz - 1) bytes into buf.
 * Returns 0 on success.
 */
int hq_adapter_get_last_error(HqAdapter *adapter,
                               char *buf, size_t buf_sz);

/* ---------------------------------------------------------------
 *  EOV2 session reader
 * --------------------------------------------------------------- */

/**
 * Read the video_track.path from the eov2 result session file.
 *
 * Expected file: <work_dir>/job.<codec>.<preset>.eov2.session.json
 *
 * @param work_dir     Absolute path to the task_N directory.
 * @param codec        HQ codec identifier used for the job.
 * @param preset       HQ preset identifier used for the job.
 * @param out_path     Buffer for the absolute video track path.
 * @param out_path_sz  Size of out_path buffer.
 * @return 0 on success, -1 if the session cannot be read or parsed.
 */
int hq_read_video_track_path(const char *work_dir,
                              const char *codec,
                              const char *preset,
                              char *out_path,
                              size_t out_path_sz);

/**
 * Recursively remove a directory tree (task_N cleanup).
 * Does not follow symbolic links outside the target directory.
 * Returns 0 on success, -1 on error.
 */
int hq_remove_directory_tree(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* HQ_ADAPTER_H */
