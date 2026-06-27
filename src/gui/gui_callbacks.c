/*  gui_callbacks.c
 *  Implementation of ConverterCallbacks and thread handling.
 */

#include "gui_window.h"
#include "mux.h"
#include <glib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Forward declarations of helper functions */
static gboolean update_log_idle(gpointer data);
static gboolean update_progress_idle(gpointer data);
static gboolean update_stage_idle(gpointer data);
static gboolean update_status_idle(gpointer data);
static gboolean finish_conversion_idle(gpointer data);
static void log_update_data_free(gpointer data);
static void stage_update_data_free(gpointer data);
static void status_update_data_free(gpointer data);
static void finish_update_data_free(gpointer data);

typedef struct {
    AppWidgets *w;
    char *msg;
} LogUpdateData;

typedef struct {
    AppWidgets *w;
    float percent;
    float fps;
    float eta;
    gboolean analysis_mode;
} ProgressUpdateData;

typedef struct {
    AppWidgets *w;
    char *stage;
} StageUpdateData;

typedef struct {
    AppWidgets *w;
    char *text;
} StatusUpdateData;

typedef struct {
    AppWidgets *w;
    ConverterError result;
    char *message;
} FinishUpdateData;

static AppWidgets *g_widgets = NULL;

static gboolean codec_is_mux(const char *codec)
{
    return codec && strcmp(codec, "mux") == 0;
}

static void reset_stop_state(AppWidgets *w)
{
    if (w)
        w->stop_requested = 0;
}

static gboolean file_is_regular_readable(const char *path)
{
    struct stat st;

    return path &&
           path[0] != '\0' &&
           stat(path, &st) == 0 &&
           S_ISREG(st.st_mode) &&
           access(path, R_OK) == 0;
}

static void resolve_effective_output_dir(const ConvertOptions *opts, char *out_dir, size_t out_dir_sz)
{
    const char *home;

    if (!opts || !out_dir || out_dir_sz == 0)
        return;

    if (opts->output_dir[0] != '\0') {
        strncpy(out_dir, opts->output_dir, out_dir_sz - 1);
        out_dir[out_dir_sz - 1] = '\0';
        return;
    }

    home = g_get_home_dir();
    if (!home || home[0] == '\0')
        home = ".";

    g_snprintf(out_dir, out_dir_sz, "%s/ffmpeg_converter", home);
}

static ConverterError run_gui_mux_postprocess(const ConvertOptions *opts,
                                              const ConverterCallbacks *cb,
                                              const char *input_file)
{
    ConvertOptions file_opts;
    MuxOptions mux_opts;
    char effective_output_dir[1024];

    memset(&file_opts, 0, sizeof(file_opts));
    file_opts = *opts;
    strcpy(file_opts.codec, "copy");
    resolve_effective_output_dir(opts, effective_output_dir, sizeof(effective_output_dir));
    strncpy(file_opts.output_dir, effective_output_dir, sizeof(file_opts.output_dir) - 1);
    file_opts.output_dir[sizeof(file_opts.output_dir) - 1] = '\0';

    memset(&mux_opts, 0, sizeof(mux_opts));
    converter_make_output_name(input_file, &file_opts, mux_opts.intermediate_file, sizeof(mux_opts.intermediate_file));
    strncpy(mux_opts.video_track_file, opts->video_track_path, sizeof(mux_opts.video_track_file) - 1);
    mux_opts.video_track_file[sizeof(mux_opts.video_track_file) - 1] = '\0';
    strncpy(mux_opts.output_file, mux_opts.intermediate_file, sizeof(mux_opts.output_file) - 1);
    mux_opts.output_file[sizeof(mux_opts.output_file) - 1] = '\0';
    mux_opts.overwrite = opts->overwrite;

    return mux_run_postprocess(&mux_opts, opts, cb);
}

static ConverterError run_gui_m4v_job(AppWidgets *w,
                                      const ConverterCallbacks *cb,
                                      char **file_list,
                                      int file_count)
{
    int i;
    int success_count = 0;
    int fail_count = 0;
    ConverterError final_err = ERR_OK;

    for (i = 0; i < file_count; ++i) {
        char output_file[1024];
        char detail[256];
        char error_text[1024];
        ConverterError err;

        if (w->stop_requested)
            return ERR_SKIP_FILE;

        if (cb->on_file_begin)
            cb->on_file_begin(file_list[i], i + 1, file_count);

        err = m4v_validate_input_supported(file_list[i], detail, sizeof(detail), NULL, 0);
        if (err != ERR_OK) {
            LogUpdateData *data = g_new0(LogUpdateData, 1);
            data->w = w;
            data->msg = g_strdup_printf("Apple m4v skipped: %s (%s)\n", file_list[i], detail);
            g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);
            if (cb->on_file_end)
                cb->on_file_end(file_list[i], err);
            fail_count += 1;
            final_err = err;
            continue;
        }

        m4v_make_output_name(file_list[i], w->output_dir_path, output_file, sizeof(output_file));
        err = m4v_create_from_input(file_list[i],
                                    output_file,
                                    &w->pending_m4v_options,
                                    gtk_check_button_get_active(GTK_CHECK_BUTTON(w->overwrite_check)) ? 1 : 0,
                                    &w->stop_requested,
                                    cb,
                                    error_text,
                                    sizeof(error_text));
        if (err == ERR_OK) {
            LogUpdateData *data = g_new0(LogUpdateData, 1);
            data->w = w;
            data->msg = g_strdup_printf("Apple m4v completed: %s\n", output_file);
            g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);
            success_count += 1;
        } else if (err == ERR_SKIP_FILE) {
            if (cb->on_file_end)
                cb->on_file_end(file_list[i], err);
            return ERR_SKIP_FILE;
        } else {
            LogUpdateData *data = g_new0(LogUpdateData, 1);
            data->w = w;
            data->msg = g_strdup_printf("Apple m4v failed: %s\n", error_text[0] != '\0' ? error_text : converter_error_string(err));
            g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);
            fail_count += 1;
            final_err = err;
        }

        if (cb->on_file_end)
            cb->on_file_end(file_list[i], err);
    }

    if (success_count > 0 && fail_count == 0)
        return ERR_OK;
    if (success_count == 0 && fail_count > 0)
        return final_err;
    return final_err == ERR_OK ? ERR_UNKNOWN : final_err;
}

/* ------------------------------------------------------------------ */
/* ConverterCallbacks implementation -------------------------------- */
/* ------------------------------------------------------------------ */
static void on_file_begin(const char *filename, int index, int total)
{
    LogUpdateData *data = g_new0(LogUpdateData, 1);
    data->w = g_widgets;
    data->msg = g_strdup_printf("[%d/%d] Processing: %s\n", index, total, filename);
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);

    StatusUpdateData *status = g_new0(StatusUpdateData, 1);
    status->w = g_widgets;
    status->text = g_strdup_printf("[%d/%d] %s", index, total, filename);
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_status_idle, status, status_update_data_free);
}

static void on_file_end(const char *filename, ConverterError status)
{
    const char *msg = converter_error_string(status);
    LogUpdateData *data = g_new0(LogUpdateData, 1);
    data->w = g_widgets;
    data->msg = g_strdup_printf("%s: %s\n", filename, msg);
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);
}

static void on_stage(const char *stage)
{
    StageUpdateData *data = g_new0(StageUpdateData, 1);
    data->w = g_widgets;
    data->stage = g_strdup_printf("Stage: %s", stage);
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_stage_idle, data, stage_update_data_free);

    StatusUpdateData *status = g_new0(StatusUpdateData, 1);
    status->w = g_widgets;
    status->text = g_strdup_printf("Stage: %s", stage);
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_status_idle, status, status_update_data_free);
}

static void on_progress_encode(float percent, float fps, float eta)
{
    ProgressUpdateData *data = g_new0(ProgressUpdateData, 1);
    data->w = g_widgets;
    data->percent = percent;
    data->fps = fps;
    data->eta = eta;
    data->analysis_mode = FALSE;
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_progress_idle, data, g_free);
}

static void on_progress_analysis(float percent, float eta)
{
    ProgressUpdateData *data = g_new0(ProgressUpdateData, 1);
    data->w = g_widgets;
    data->percent = percent;
    data->fps = 0.0f;
    data->eta = eta;
    data->analysis_mode = TRUE;
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_progress_idle, data, g_free);
}

static void on_message(const char *text)
{
    LogUpdateData *data = g_new0(LogUpdateData, 1);
    data->w = g_widgets;
    data->msg = g_strdup_printf("%s\n", text);
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);
}

static void on_error(const char *text, ConverterError code)
{
    LogUpdateData *data = g_new0(LogUpdateData, 1);
    data->w = g_widgets;
    data->msg = g_strdup_printf("ERROR: %s (%s)\n",
                                text,
                                converter_error_string(code));
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);

    StatusUpdateData *status = g_new0(StatusUpdateData, 1);
    status->w = g_widgets;
    status->text = g_strdup_printf("ERROR: %s", text);
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_status_idle, status, status_update_data_free);
}

static void on_complete(void)
{
    LogUpdateData *log_data = g_new0(LogUpdateData, 1);
    log_data->w = g_widgets;
    log_data->msg = g_strdup("\nAll files processed.\n");
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, log_data, log_update_data_free);
}

/* ------------------------------------------------------------------ */
/* Helper: UI updates ----------------------------------------------- */
/* ------------------------------------------------------------------ */
static gboolean update_log_idle(gpointer data)
{
    LogUpdateData *payload = (LogUpdateData *)data;
    if (!payload || !payload->w || !payload->msg || payload->w->shutting_down)
        return G_SOURCE_REMOVE;

    AppWidgets *w = payload->w;
    const gchar *msg = payload->msg;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(w->log_buffer, &end);
    gtk_text_buffer_insert(w->log_buffer, &end, msg, -1);
    /* autoscroll */
    GtkTextMark *mark = gtk_text_buffer_create_mark(w->log_buffer, NULL, &end, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(w->log_view), mark, 0.0, TRUE, 0.0, 0.0);
    gtk_text_buffer_delete_mark(w->log_buffer, mark);

    return G_SOURCE_REMOVE;
}

static void format_eta(float eta, char *buf, size_t sz)
{
    if (!isfinite(eta) || eta <= 0) {
        snprintf(buf, sz, "ETA --:--:--");
        return;
    }
    int t = (int)eta;
    int h = t / 3600;
    int m = (t % 3600) / 60;
    int s = t % 60;
    snprintf(buf, sz, "ETA %02d:%02d:%02d", h, m, s);
}

static gboolean update_progress_idle(gpointer data)
{
    ProgressUpdateData *payload = (ProgressUpdateData *)data;
    if (!payload || !payload->w || payload->w->shutting_down)
        return G_SOURCE_REMOVE;

    AppWidgets *w = payload->w;
    float percent = payload->percent;
    float fps = payload->fps;
    float eta = payload->eta;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->progress_bar), percent / 100.0);

    char txt[128];
    if (payload->analysis_mode)
        snprintf(txt, sizeof(txt), "%d%%", (int)(percent + 0.5));
    else if (fps > 0)
        snprintf(txt, sizeof(txt), "%.0f fps", fps);
    else
        snprintf(txt, sizeof(txt), "%d%%", (int)(percent + 0.5));

    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(w->progress_bar), txt);

    char eta_buf[32];
    char status_buf[160];
    format_eta(eta, eta_buf, sizeof(eta_buf));
    if (payload->analysis_mode) {
        snprintf(status_buf, sizeof(status_buf), "Analysis %.0f%% %s", percent, eta_buf);
    } else if (fps > 0.0f) {
        snprintf(status_buf, sizeof(status_buf), "Encoding %.0f%% %.0f fps %s", percent, fps, eta_buf);
    } else {
        snprintf(status_buf, sizeof(status_buf), "Encoding %.0f%% %s", percent, eta_buf);
    }
    gtk_label_set_text(GTK_LABEL(w->status_label), status_buf);

    return G_SOURCE_REMOVE;
}

static gboolean update_stage_idle(gpointer data)
{
    StageUpdateData *payload = (StageUpdateData *)data;
    if (!payload || !payload->w || !payload->stage || payload->w->shutting_down)
        return G_SOURCE_REMOVE;

    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(payload->w->progress_bar), payload->stage);
    return G_SOURCE_REMOVE;
}

static gboolean finish_conversion_idle(gpointer data)
{
    FinishUpdateData *payload = (FinishUpdateData *)data;
    if (!payload || !payload->w || payload->w->shutting_down)
        return G_SOURCE_REMOVE;

    set_running_ui_state(payload->w, FALSE);

    if (payload->result == ERR_OK) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(payload->w->progress_bar), 1.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(payload->w->progress_bar), "100%");
        gtk_label_set_text(GTK_LABEL(payload->w->status_label), "Completed");
        clear_file_list(payload->w);
    } else if (payload->result == ERR_SKIP_FILE) {
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(payload->w->progress_bar), "Stopped");
        gtk_label_set_text(GTK_LABEL(payload->w->status_label), "Stopped");
    } else {
        char status_text[256];
        snprintf(status_text,
                 sizeof(status_text),
                 "Finished: %s",
                 payload->message ? payload->message : converter_error_string(payload->result));
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(payload->w->progress_bar), "Finished with errors");
        gtk_label_set_text(GTK_LABEL(payload->w->status_label), status_text);
    }

    return G_SOURCE_REMOVE;
}

static gboolean update_status_idle(gpointer data)
{
    StatusUpdateData *payload = (StatusUpdateData *)data;
    if (!payload || !payload->w || !payload->text || payload->w->shutting_down)
        return G_SOURCE_REMOVE;

    gtk_label_set_text(GTK_LABEL(payload->w->status_label), payload->text);
    return G_SOURCE_REMOVE;
}

static void log_update_data_free(gpointer data)
{
    LogUpdateData *payload = (LogUpdateData *)data;
    if (!payload)
        return;
    g_free(payload->msg);
    g_free(payload);
}

static void stage_update_data_free(gpointer data)
{
    StageUpdateData *payload = (StageUpdateData *)data;
    if (!payload)
        return;
    g_free(payload->stage);
    g_free(payload);
}

static void status_update_data_free(gpointer data)
{
    StatusUpdateData *payload = (StatusUpdateData *)data;
    if (!payload)
        return;
    g_free(payload->text);
    g_free(payload);
}

static void finish_update_data_free(gpointer data)
{
    FinishUpdateData *payload = (FinishUpdateData *)data;
    if (!payload)
        return;
    g_free(payload->message);
    g_free(payload);
}

/* ------------------------------------------------------------------ */
/* Thread runner ----------------------------------------------------- */
/* ------------------------------------------------------------------ */
static gpointer run_converter(gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;
    ConverterError err = ERR_UNKNOWN;
    GuiJobKind job_kind;

    /* Gather options and files from GUI */
    ConvertOptions opts;
    char **file_list = NULL;
    int   file_count = 0;
    collect_options_from_gui(w, &opts, &file_list, &file_count);
    job_kind = w->active_job_kind;

    if (job_kind == GUI_JOB_M4V) {
        if (file_count <= 0) {
            LogUpdateData *data = g_new0(LogUpdateData, 1);
            data->w = w;
            data->msg = g_strdup("Apple m4v requires at least one source file\n");
            g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);
            err = ERR_INVALID_OPTIONS;
            goto cleanup;
        }

        g_widgets = w;
        {
            ConverterCallbacks cb = {
                .on_file_begin = on_file_begin,
                .on_file_end = on_file_end,
                .on_stage = on_stage,
                .on_progress_encode = on_progress_encode,
                .on_progress_analysis = on_progress_analysis,
                .on_message = on_message,
                .on_error = on_error,
                .on_complete = on_complete
            };
            err = run_gui_m4v_job(w, &cb, file_list, file_count);
        }
        goto cleanup;
    }

    /* Create converter instance */
    Converter *c = converter_create();
    if (!c) {
        LogUpdateData *data = g_new0(LogUpdateData, 1);
        data->w = w;
        data->msg = g_strdup("Failed to create converter\n");
        g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);
        err = ERR_UNKNOWN;
        goto cleanup;
    }

    g_mutex_lock(&w->thread_lock);
    w->current_converter = c;
    g_mutex_unlock(&w->thread_lock);

    /* Prepare callbacks with captured widget pointer */
    ConverterCallbacks cb = {
        .on_file_begin        = on_file_begin,
        .on_file_end          = on_file_end,
        .on_stage             = on_stage,
        .on_progress_encode   = on_progress_encode,
        .on_progress_analysis= on_progress_analysis,
        .on_message           = on_message,
        .on_error             = on_error,
        .on_complete          = on_complete
    };

    if (codec_is_mux(opts.codec)) {
        if (file_count != 1) {
            LogUpdateData *data = g_new0(LogUpdateData, 1);
            data->w = w;
            data->msg = g_strdup("Mux mode requires exactly one source file\n");
            g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);
            err = ERR_INVALID_OPTIONS;
            goto cleanup;
        }

        if (!file_is_regular_readable(opts.video_track_path)) {
            LogUpdateData *data = g_new0(LogUpdateData, 1);
            data->w = w;
            data->msg = g_strdup("Mux mode requires a readable video-track file\n");
            g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);
            err = ERR_INVALID_OPTIONS;
            goto cleanup;
        }
    }

    g_widgets = w;
    converter_set_callbacks(c, &cb);
    {
        ConvertOptions work_opts = opts;

        if (codec_is_mux(opts.codec)) {
            strcpy(work_opts.codec, "copy");
            work_opts.profile = 0;
            work_opts.deblock = 0;
        }

        err = converter_set_options(c, &work_opts);
        if (err != ERR_OK)
            goto cleanup;
    }

    err = converter_process_files(c, (const char**)file_list, file_count);
    if (err == ERR_OK && codec_is_mux(opts.codec))
        err = run_gui_mux_postprocess(&opts, &cb, file_list[0]);

    if (err == ERR_SKIP_FILE) {
        LogUpdateData *data = g_new0(LogUpdateData, 1);
        data->w = w;
        data->msg = g_strdup("Conversion stopped\n");
        g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);
    } else if (err != ERR_OK) {
        LogUpdateData *data = g_new0(LogUpdateData, 1);
        data->w = w;
        data->msg = g_strdup_printf("Finished with errors: %s\n", converter_error_string(err));
        g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);
    } else {
        LogUpdateData *data = g_new0(LogUpdateData, 1);
        data->w = w;
        data->msg = g_strdup("Conversion completed\n");
        g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);
    }

    /* Clean up */
cleanup:
    g_mutex_lock(&w->thread_lock);
    if (w->current_converter == c)
        w->current_converter = NULL;
    w->active_job_kind = GUI_JOB_NONE;
    g_mutex_unlock(&w->thread_lock);

    converter_destroy(c);
    for (int i = 0; i < file_count; ++i)
        g_free(file_list[i]);
    g_free(file_list);

    g_mutex_lock(&w->thread_lock);
    if (w->worker_thread == g_thread_self())
        w->worker_thread = NULL;
    g_mutex_unlock(&w->thread_lock);

    {
        FinishUpdateData *finish = g_new0(FinishUpdateData, 1);
        finish->w = w;
        finish->result = err;
        finish->message = g_strdup(converter_error_string(err));
        g_idle_add_full(G_PRIORITY_HIGH_IDLE,
                        finish_conversion_idle,
                        finish,
                        finish_update_data_free);
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/* Start / stop conversion -------------------------------------------- */
/* ------------------------------------------------------------------ */
void start_conversion(AppWidgets *w)
{
    if (!w || w->shutting_down)
        return;

    g_mutex_lock(&w->thread_lock);
    if (w->worker_thread != NULL) {
        g_mutex_unlock(&w->thread_lock);
        return;
    }
    w->pending_job_kind = GUI_JOB_CONVERT;
    w->active_job_kind = GUI_JOB_CONVERT;
    g_mutex_unlock(&w->thread_lock);

    reset_stop_state(w);

    set_running_ui_state(w, TRUE);

    /* Clear progress and log */
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->progress_bar), 0.0);
    gtk_text_buffer_set_text(w->log_buffer, "", -1);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(w->progress_bar), "");
    gtk_label_set_text(GTK_LABEL(w->status_label), "Starting...");

    {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(w->log_buffer, &end);
        gtk_text_buffer_insert(w->log_buffer, &end, "Conversion started\n", -1);
    }

    /* Launch thread */
    g_mutex_lock(&w->thread_lock);
    w->worker_thread = g_thread_new("converter", run_converter, w);
    g_mutex_unlock(&w->thread_lock);
}

void start_m4v_creation(AppWidgets *w)
{
    if (!w || w->shutting_down)
        return;

    g_mutex_lock(&w->thread_lock);
    if (w->worker_thread != NULL) {
        g_mutex_unlock(&w->thread_lock);
        return;
    }
    w->pending_job_kind = GUI_JOB_M4V;
    w->active_job_kind = GUI_JOB_M4V;
    g_mutex_unlock(&w->thread_lock);

    reset_stop_state(w);
    set_running_ui_state(w, TRUE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->progress_bar), 0.0);
    gtk_text_buffer_set_text(w->log_buffer, "", -1);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(w->progress_bar), "");
    gtk_label_set_text(GTK_LABEL(w->status_label), "Apple m4v: starting...");

    {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(w->log_buffer, &end);
        gtk_text_buffer_insert(w->log_buffer, &end, "Apple m4v started\n", -1);
    }

    g_mutex_lock(&w->thread_lock);
    w->worker_thread = g_thread_new("m4v", run_converter, w);
    g_mutex_unlock(&w->thread_lock);
}

void stop_conversion(AppWidgets *w)
{
    if (!w)
        return;

    /* Signal converter to stop */
    g_mutex_lock(&w->thread_lock);
    if (w->current_converter)
        converter_stop(w->current_converter);
    w->stop_requested = 1;
    g_mutex_unlock(&w->thread_lock);

    {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(w->log_buffer, &end);
        gtk_text_buffer_insert(w->log_buffer, &end, "Stop requested\n", -1);
    }

    gtk_label_set_text(GTK_LABEL(w->status_label), "Stopping...");
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(w->progress_bar), "Stopping...");
}

void shutdown_conversion(AppWidgets *w)
{
    if (!w)
        return;

    g_mutex_lock(&w->thread_lock);
    w->shutting_down = TRUE;
    g_widgets = NULL;
    w->stop_requested = 1;

    if (w->current_converter)
        converter_stop(w->current_converter);

    GThread *thread = w->worker_thread;
    w->worker_thread = NULL;
    g_mutex_unlock(&w->thread_lock);

    if (thread)
        g_thread_join(thread);
}
