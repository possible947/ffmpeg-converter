/*  gui_callbacks.c
 *  Implementation of ConverterCallbacks and thread handling.
 */

#include "gui_window.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>

/* Forward declarations of helper functions */
static gboolean update_log_idle(gpointer data);
static gboolean update_progress_idle(gpointer data);
static gboolean update_stage_idle(gpointer data);
static gboolean set_widget_sensitive_idle(gpointer data);
static gboolean clear_file_list_idle(gpointer data);
static gboolean update_status_idle(gpointer data);
static void log_update_data_free(gpointer data);
static void stage_update_data_free(gpointer data);
static void status_update_data_free(gpointer data);

typedef struct {
    AppWidgets *w;
    char *msg;
} LogUpdateData;

typedef struct {
    AppWidgets *w;
    float percent;
    float fps;
    float eta;
} ProgressUpdateData;

typedef struct {
    AppWidgets *w;
    char *stage;
} StageUpdateData;

typedef struct {
    GtkWidget *widget;
    gboolean sensitive;
} WidgetSensitiveData;

typedef struct {
    AppWidgets *w;
    char *text;
} StatusUpdateData;

static AppWidgets *g_widgets = NULL;

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
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_progress_idle, data, g_free);
}

static void on_progress_analysis(float percent, float eta)
{
    ProgressUpdateData *data = g_new0(ProgressUpdateData, 1);
    data->w = g_widgets;
    data->percent = percent;
    data->fps = 0.0f;
    data->eta = eta;
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_progress_idle, data, g_free);
}

static void on_message(const char *text)
{
    LogUpdateData *data = g_new0(LogUpdateData, 1);
    data->w = g_widgets;
    data->msg = g_strdup_printf("%s\n", text);
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);

    StatusUpdateData *status = g_new0(StatusUpdateData, 1);
    status->w = g_widgets;
    status->text = g_strdup(text);
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_status_idle, status, status_update_data_free);
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

    StatusUpdateData *status = g_new0(StatusUpdateData, 1);
    status->w = g_widgets;
    status->text = g_strdup("All files processed.");
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_status_idle, status, status_update_data_free);

    WidgetSensitiveData *start_data = g_new0(WidgetSensitiveData, 1);
    start_data->widget = g_widgets ? g_widgets->start_btn : NULL;
    start_data->sensitive = TRUE;
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, set_widget_sensitive_idle, start_data, g_free);

    WidgetSensitiveData *stop_data = g_new0(WidgetSensitiveData, 1);
    stop_data->widget = g_widgets ? g_widgets->stop_btn : NULL;
    stop_data->sensitive = FALSE;
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, set_widget_sensitive_idle, stop_data, g_free);

    if (g_widgets)
        g_idle_add_full(G_PRIORITY_HIGH_IDLE, clear_file_list_idle, g_widgets, NULL);
}

/* ------------------------------------------------------------------ */
/* Helper: UI updates ----------------------------------------------- */
/* ------------------------------------------------------------------ */
static gboolean update_log_idle(gpointer data)
{
    LogUpdateData *payload = (LogUpdateData *)data;
    if (!payload || !payload->w || !payload->msg)
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

static gboolean update_progress_idle(gpointer data)
{
    ProgressUpdateData *payload = (ProgressUpdateData *)data;
    if (!payload || !payload->w)
        return G_SOURCE_REMOVE;

    AppWidgets *w = payload->w;
    float percent = payload->percent;
    float fps = payload->fps;
    float eta = payload->eta;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->progress_bar), percent / 100.0);

    char txt[128];
    if (fps > 0)
        snprintf(txt, sizeof(txt), "%.0f fps", fps);
    else
        snprintf(txt, sizeof(txt), "%d%%", (int)(percent + 0.5));

    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(w->progress_bar), txt);

    (void)eta;
    return G_SOURCE_REMOVE;
}

static gboolean update_stage_idle(gpointer data)
{
    StageUpdateData *payload = (StageUpdateData *)data;
    if (!payload || !payload->w || !payload->stage)
        return G_SOURCE_REMOVE;

    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(payload->w->progress_bar), payload->stage);
    return G_SOURCE_REMOVE;
}

static gboolean set_widget_sensitive_idle(gpointer data)
{
    WidgetSensitiveData *payload = (WidgetSensitiveData *)data;
    if (!payload || !payload->widget)
        return G_SOURCE_REMOVE;

    gtk_widget_set_sensitive(payload->widget, payload->sensitive);
    return G_SOURCE_REMOVE;
}

static gboolean clear_file_list_idle(gpointer data)
{
    AppWidgets *w = (AppWidgets *)data;
    if (!w)
        return G_SOURCE_REMOVE;

    clear_file_list(w);
    return G_SOURCE_REMOVE;
}

static gboolean update_status_idle(gpointer data)
{
    StatusUpdateData *payload = (StatusUpdateData *)data;
    if (!payload || !payload->w || !payload->text)
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

/* ------------------------------------------------------------------ */
/* Thread runner ----------------------------------------------------- */
/* ------------------------------------------------------------------ */
static gpointer run_converter(gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;

    /* Gather options and files from GUI */
    ConvertOptions opts;
    char **file_list = NULL;
    int   file_count = 0;
    collect_options_from_gui(w, &opts, &file_list, &file_count);

    /* Create converter instance */
    Converter *c = converter_create();
    if (!c) {
        LogUpdateData *data = g_new0(LogUpdateData, 1);
        data->w = w;
        data->msg = g_strdup("Failed to create converter\n");
        g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);
        return NULL;
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

    g_widgets = w;
    converter_set_callbacks(c, &cb);
    converter_set_options(c, &opts);

    ConverterError err = converter_process_files(c, (const char**)file_list, file_count);

    if (err != ERR_OK) {
        LogUpdateData *data = g_new0(LogUpdateData, 1);
        data->w = w;
        data->msg = g_strdup("Processing finished with errors.\n");
        g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_log_idle, data, log_update_data_free);
    }

    /* Clean up */
    converter_destroy(c);
    for (int i = 0; i < file_count; ++i)
        g_free(file_list[i]);
    g_free(file_list);

    g_mutex_lock(&w->thread_lock);
    w->current_converter = NULL;
    g_mutex_unlock(&w->thread_lock);

    return NULL;
}

/* ------------------------------------------------------------------ */
/* Start / stop conversion -------------------------------------------- */
/* ------------------------------------------------------------------ */
void start_conversion(AppWidgets *w)
{
    /* Disable start, enable stop */
    gtk_widget_set_sensitive(w->start_btn, FALSE);
    gtk_widget_set_sensitive(w->stop_btn, TRUE);

    /* Clear progress and log */
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->progress_bar), 0.0);
    gtk_text_buffer_set_text(w->log_buffer, "", -1);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(w->progress_bar), "");
    gtk_label_set_text(GTK_LABEL(w->status_label), "Starting...");

    /* Launch thread */
    g_mutex_lock(&w->thread_lock);
    w->worker_thread = g_thread_new("converter", run_converter, w);
    g_mutex_unlock(&w->thread_lock);
}

void stop_conversion(AppWidgets *w)
{
    /* Signal converter to stop */
    g_mutex_lock(&w->thread_lock);
    if (w->current_converter)
        converter_stop(w->current_converter);
    g_mutex_unlock(&w->thread_lock);

    gtk_widget_set_sensitive(w->stop_btn, FALSE);
    gtk_widget_set_sensitive(w->start_btn, TRUE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->progress_bar), 0.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(w->progress_bar), "Stopped");
    gtk_label_set_text(GTK_LABEL(w->status_label), "Stopped");
}
