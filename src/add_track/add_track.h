#ifndef ADD_TRACK_H
#define ADD_TRACK_H

#include "converter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------
 *  Request
 * --------------------------------------------------------------- */

/**
 * AddTrackRequest — input for the Manual Add Track pipeline.
 *
 * source_file:
 *   Exactly one source media file to process (audio path).
 *   Caller-allocated; must outlive add_track_execute().
 *   Must NOT be NULL or empty.
 *
 * convert_options:
 *   Authoritative option source. add_track_execute() creates a local
 *   copy and adapts codec="copy", preset="default", deblock=0.
 *   Caller-allocated; must outlive add_track_execute().
 *   Must NOT be NULL. convert_options->video_track_path must be set.
 */
typedef struct {
    const char *source_file;
    const ConvertOptions *convert_options;
} AddTrackRequest;

/* ---------------------------------------------------------------
 *  Result
 * --------------------------------------------------------------- */

/**
 * AddTrackResult — output from add_track_execute().
 *
 * output_file:
 *   Final output path. Valid only when err == ERR_OK.
 *   1024 bytes: matches MuxOptions.output_file size.
 */
typedef struct {
    char output_file[1024];
} AddTrackResult;

/* ---------------------------------------------------------------
 *  Execution
 * --------------------------------------------------------------- */

/**
 * Execute the Manual Add Track pipeline for a single source file.
 *
 * Contract:
 *  - Single file: processes exactly one source_file.
 *  - Synchronous: blocks until conversion and mux complete.
 *  - Caller-owned Converter: does not create or destroy converter.
 *  - Thread-neutral: may be called from any thread.
 *  - ConverterCallbacks must already be set on converter.
 *
 * Error contract:
 *  - Returns ConverterError (ERR_OK on success).
 *  - Emits user-visible messages via converter's Callbacks.
 *  - If converter fails, mux is NOT called; error returned immediately.
 *  - If mux fails, error returned; output_file in result is empty.
 *
 * Cancellation:
 *  - Checks stop flag before converter_process_files().
 *  - Checks stop flag after converter_process_files(), before mux.
 *  - Does NOT interrupt mux (current behavior preserved).
 *
 * @param converter Caller-owned Converter (must have callbacks set).
 * @param request AddTrackRequest (must not be NULL or contain NULL fields).
 * @param result AddTrackResult (caller-allocated; populated on success).
 * @return ERR_OK on success, or a ConverterError indicating failure.
 */
ConverterError add_track_execute(
    Converter *converter,
    const AddTrackRequest *request,
    AddTrackResult *result
);

#ifdef __cplusplus
}
#endif

#endif /* ADD_TRACK_H */
