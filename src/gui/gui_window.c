/*  gui_window.c
 *  Build the GTK4 UI and manage user interaction.
 */

#include "gui_window.h"
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <gio/gio.h>

/* Forward declarations */
static void update_dependent_widgets(AppWidgets *w);
static void on_codec_changed(GObject *obj, GParamSpec *pspec, AppWidgets *w);
static void on_audio_norm_changed(GObject *obj, GParamSpec *pspec, AppWidgets *w);
static void on_add_files_clicked(GtkButton *button, AppWidgets *w);
static void on_add_files_response(GObject *source, GAsyncResult *res, AppWidgets *w);
static void on_output_dir_clicked(GtkButton *button, AppWidgets *w);
static void on_output_dir_response(GObject *source, GAsyncResult *res, AppWidgets *w);
static void on_remove_file_clicked(GtkButton *button, AppWidgets *w);
static void on_clear_list_clicked(GtkButton *button, AppWidgets *w);
static void on_start_clicked(GtkButton *button, AppWidgets *w);
static void on_stop_clicked(GtkButton *button, AppWidgets *w);
static void set_output_dir(AppWidgets *w, const char *path);
static char *get_dropdown_text(GtkWidget *dropdown);

/* ------------------------------------------------------------------ */
/* Build the main window and all widgets                               */
/* ------------------------------------------------------------------ */
GtkWidget* create_main_window(GtkApplication *app, AppWidgets *w)
{
    /* ---------- Main container ---------- */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);

    /* ---------- Codec combo ---------- */
    {
        const char *items[] = {"copy", "prores", "prores_ks", "h265_mi50", NULL};
        GtkStringList *model = gtk_string_list_new(items);
        w->codec_combo = gtk_drop_down_new(G_LIST_MODEL(model), NULL);
        g_object_unref(model);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->codec_combo), 0);
        g_signal_connect(w->codec_combo, "notify::selected", G_CALLBACK(on_codec_changed), w);
    }

    /* ---------- Profile combo ---------- */
    {
        const char *items[] = {"lt", "standard", "hq", "4444", NULL};
        GtkStringList *model = gtk_string_list_new(items);
        w->profile_combo = gtk_drop_down_new(G_LIST_MODEL(model), NULL);
        g_object_unref(model);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->profile_combo), 1);
    }
    /* Initially disabled for copy/h265 */
    gtk_widget_set_sensitive(w->profile_combo, FALSE);

    /* ---------- Deblock combo ---------- */
    {
        const char *items[] = {"none", "weak", "strong", NULL};
        GtkStringList *model = gtk_string_list_new(items);
        w->deblock_combo = gtk_drop_down_new(G_LIST_MODEL(model), NULL);
        g_object_unref(model);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->deblock_combo), 0);
    }
    gtk_widget_set_sensitive(w->deblock_combo, FALSE);

    /* ---------- Audio norm combo ---------- */
    {
        const char *items[] = {
            "none",
            "peak_norm",
            "peak_norm_2pass",
            "loudness_norm",
            "loudness_norm_2pass",
            NULL
        };
        GtkStringList *model = gtk_string_list_new(items);
        w->audio_norm_combo = gtk_drop_down_new(G_LIST_MODEL(model), NULL);
        g_object_unref(model);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->audio_norm_combo), 0);
        g_signal_connect(w->audio_norm_combo, "notify::selected", G_CALLBACK(on_audio_norm_changed), w);
    }

    /* ---------- Genre combo ---------- */
    {
        const char *items[] = {"edm", "rock", "hiphop", "classical", "podcast", NULL};
        GtkStringList *model = gtk_string_list_new(items);
        w->genre_combo = gtk_drop_down_new(G_LIST_MODEL(model), NULL);
        g_object_unref(model);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->genre_combo), 0);
    }
    gtk_widget_set_sensitive(w->genre_combo, FALSE);

    /* ---------- Overwrite check ---------- */
    w->overwrite_check = gtk_check_button_new_with_label("Overwrite existing files");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(w->overwrite_check), FALSE);

    /* ---------- Output directory ---------- */
    w->output_dir_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(w->output_dir_label), 0.0f);
    gtk_widget_set_hexpand(w->output_dir_label, TRUE);
    w->output_dir_btn = gtk_button_new_with_label("Choose...");
    g_signal_connect(w->output_dir_btn, "clicked", G_CALLBACK(on_output_dir_clicked), w);
    w->output_dir_path = NULL;
    set_output_dir(w, NULL);

    /* ---------- File list ---------- */
    w->file_listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(w->file_listbox), GTK_SELECTION_SINGLE);
    w->file_paths = g_ptr_array_new_with_free_func((GDestroyNotify)g_free);

    /* ---------- Buttons ---------- */
    w->add_files_btn = gtk_button_new_with_label("Add files...");
    g_signal_connect(w->add_files_btn, "clicked", G_CALLBACK(on_add_files_clicked), w);

    w->remove_file_btn = gtk_button_new_with_label("Remove selected");
    g_signal_connect(w->remove_file_btn, "clicked", G_CALLBACK(on_remove_file_clicked), w);

    w->clear_list_btn = gtk_button_new_with_label("Clear list");
    g_signal_connect(w->clear_list_btn, "clicked", G_CALLBACK(on_clear_list_clicked), w);

    w->start_btn = gtk_button_new_with_label("Start");
    g_signal_connect(w->start_btn, "clicked", G_CALLBACK(on_start_clicked), w);

    w->stop_btn = gtk_button_new_with_label("Stop");
    gtk_widget_set_sensitive(w->stop_btn, FALSE);
    g_signal_connect(w->stop_btn, "clicked", G_CALLBACK(on_stop_clicked), w);

    /* ---------- Progress bar ---------- */
    w->progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(w->progress_bar), TRUE);

    /* ---------- Log view ---------- */
    w->log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(w->log_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(w->log_view), GTK_WRAP_WORD_CHAR);
    w->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->log_view));

    GtkWidget *log_scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scroller), w->log_view);
    gtk_widget_set_vexpand(log_scroller, TRUE);
    gtk_widget_set_size_request(log_scroller, -1, 140);

    /* ---------- Status line ---------- */
    w->status_label = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(w->status_label), 0.0f);
    gtk_widget_set_hexpand(w->status_label, TRUE);

    /* ---------- Layout ---------- */
    int r = 0;
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Codec:"), 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->codec_combo, 1, r, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Profile:"), 2, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->profile_combo, 3, r, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Deblock:"), 4, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->deblock_combo, 5, r, 1, 1);
    r++;

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Audio norm:"), 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->audio_norm_combo, 1, r, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Genre:"), 2, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->genre_combo, 3, r, 1, 1);
    r++;

    gtk_grid_attach(GTK_GRID(grid), w->overwrite_check, 0, r, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Output dir:"), 2, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->output_dir_label, 3, r, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), w->output_dir_btn, 5, r, 1, 1);
    r++;

    gtk_grid_attach(GTK_GRID(grid), w->add_files_btn, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->remove_file_btn, 1, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->clear_list_btn, 2, r, 1, 1);
    r++;

    GtkWidget *file_scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(file_scroller), w->file_listbox);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(file_scroller),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(file_scroller, TRUE);
    gtk_widget_set_size_request(file_scroller, -1, 140);

    gtk_grid_attach(GTK_GRID(grid), file_scroller, 0, r, 6, 1);
    r++;

    gtk_grid_attach(GTK_GRID(grid), w->start_btn, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->stop_btn, 1, r, 1, 1);
    r++;

    gtk_grid_attach(GTK_GRID(grid), w->progress_bar, 0, r, 6, 1);
    r++;

    gtk_grid_attach(GTK_GRID(grid), log_scroller, 0, r, 6, 1);
    r++;

    gtk_grid_attach(GTK_GRID(grid), w->status_label, 0, r, 6, 1);
    r++;

    /* Wrap everything in a scrolled window */
    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), grid);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_child(GTK_WINDOW(window), scroller);

    /* Return the window widget */
    return window;
}

/* ------------------------------------------------------------------ */
/* Update visibility / sensitivity of dependent widgets                */
/* ------------------------------------------------------------------ */
static void update_dependent_widgets(AppWidgets *w)
{
    char *codec = get_dropdown_text(w->codec_combo);

    /* Profile & Deblock only for prores / prores_ks */
    gboolean profile_sensitive = g_strcmp0(codec, "copy") != 0 &&
                                 g_strcmp0(codec, "h265_mi50") != 0;
    gtk_widget_set_sensitive(w->profile_combo, profile_sensitive);
    gtk_widget_set_sensitive(w->deblock_combo, profile_sensitive);

    /* Genre only when audio_norm is loudness normalization 2-pass */
    char *audio_norm = get_dropdown_text(w->audio_norm_combo);
    gboolean genre_sensitive = g_strcmp0(audio_norm, "loudness_norm_2pass") == 0;
    gtk_widget_set_sensitive(w->genre_combo, genre_sensitive);

    g_free(audio_norm);
    g_free(codec);
}

/* ------------------------------------------------------------------ */
/* Callback: codec combo changed                                       */
/* ------------------------------------------------------------------ */
static void on_codec_changed(GObject *obj, GParamSpec *pspec, AppWidgets *w)
{
    (void)obj;
    (void)pspec;
    update_dependent_widgets(w);
}

/* ------------------------------------------------------------------ */
/* Callback: audio_norm combo changed                                   */
/* ------------------------------------------------------------------ */
static void on_audio_norm_changed(GObject *obj, GParamSpec *pspec, AppWidgets *w)
{
    (void)obj;
    (void)pspec;
    update_dependent_widgets(w);
}

/* ------------------------------------------------------------------ */
/* Add files button                                                    */
/* ------------------------------------------------------------------ */
static void on_add_files_clicked(GtkButton *button, AppWidgets *w)
{
    (void)button;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    GFile *initial = g_file_new_for_path(g_get_home_dir());
    gtk_file_dialog_set_initial_folder(dialog, initial);
    g_object_unref(initial);

    gtk_file_dialog_open_multiple(dialog, GTK_WINDOW(w->window), NULL,
                                  (GAsyncReadyCallback)on_add_files_response, w);
}

static void on_add_files_response(GObject *source, GAsyncResult *res, AppWidgets *w)
{
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;
    GListModel *files = gtk_file_dialog_open_multiple_finish(dialog, res, &error);
    if (error) {
        g_error_free(error);
        g_object_unref(dialog);
        return;
    }

    guint n_items = g_list_model_get_n_items(files);
    for (guint i = 0; i < n_items; ++i) {
        GFile *file = g_list_model_get_item(files, i);
        char *path = g_file_get_path(file);
        if (path) {
            /* Add to listbox */
            char *path_copy = g_strdup(path);
            GtkWidget *label = gtk_label_new(path_copy);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
            gtk_widget_set_halign(label, GTK_ALIGN_START);
            gtk_list_box_append(GTK_LIST_BOX(w->file_listbox), label);
            g_object_set_data(G_OBJECT(label), "file_path", path_copy);
            /* Store copy */
            g_ptr_array_add(w->file_paths, path_copy);
        }
        g_free(path);
        g_object_unref(file);
    }

    g_object_unref(files);
    g_object_unref(dialog);
}

static void on_output_dir_clicked(GtkButton *button, AppWidgets *w)
{
    (void)button;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    if (w->output_dir_path && w->output_dir_path[0] != '\0') {
        GFile *current = g_file_new_for_path(w->output_dir_path);
        gtk_file_dialog_set_initial_folder(dialog, current);
        g_object_unref(current);
    }

    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(w->window), NULL,
                                  (GAsyncReadyCallback)on_output_dir_response, w);
}

static void on_output_dir_response(GObject *source, GAsyncResult *res, AppWidgets *w)
{
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;
    GFile *file = gtk_file_dialog_select_folder_finish(dialog, res, &error);
    if (error) {
        g_error_free(error);
        g_object_unref(dialog);
        return;
    }

    if (file) {
        char *path = g_file_get_path(file);
        if (path)
            set_output_dir(w, path);
        g_free(path);
        g_object_unref(file);
    }

    g_object_unref(dialog);
}

static void on_remove_file_clicked(GtkButton *button, AppWidgets *w)
{
    (void)button;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(w->file_listbox));
    if (!row)
        return;

    GtkWidget *child = gtk_list_box_row_get_child(row);
    char *path = NULL;
    if (child)
        path = (char *)g_object_get_data(G_OBJECT(child), "file_path");

    if (path)
        g_ptr_array_remove(w->file_paths, path);

    gtk_list_box_remove(GTK_LIST_BOX(w->file_listbox), GTK_WIDGET(row));
}

static void on_clear_list_clicked(GtkButton *button, AppWidgets *w)
{
    (void)button;
    clear_file_list(w);
}

static void on_start_clicked(GtkButton *button, AppWidgets *w)
{
    (void)button;
    start_conversion(w);
}

static void on_stop_clicked(GtkButton *button, AppWidgets *w)
{
    (void)button;
    stop_conversion(w);
}

/* ------------------------------------------------------------------ */
/* Helper: collect options from GUI                                   */
/* ------------------------------------------------------------------ */
void collect_options_from_gui(AppWidgets *w,
                              ConvertOptions *opts,
                              char ***out_files,
                              int   *out_count)
{
    /* ----- codec ----- */
    char *codec = get_dropdown_text(w->codec_combo);
    g_strlcpy(opts->codec, codec ? codec : "", sizeof(opts->codec));
    g_free(codec);

    /* ----- profile ----- */
    if (gtk_widget_get_sensitive(w->profile_combo)) {
        opts->profile = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(w->profile_combo)) + 1;
    } else {
        opts->profile = 0;   /* not used for copy/h265 */
    }

    /* ----- deblock ----- */
    if (gtk_widget_get_sensitive(w->deblock_combo)) {
        opts->deblock = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(w->deblock_combo)) + 1;
    } else {
        opts->deblock = 0;
    }

    /* ----- audio norm ----- */
    char *norm = get_dropdown_text(w->audio_norm_combo);
    g_strlcpy(opts->audio_norm, norm ? norm : "", sizeof(opts->audio_norm));
    g_free(norm);

    /* ----- genre ----- */
    if (gtk_widget_get_sensitive(w->genre_combo)) {
        opts->genre = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(w->genre_combo)) + 1;
    } else {
        opts->genre = 0;
    }

    /* ----- overwrite ----- */
    opts->overwrite = gtk_check_button_get_active(GTK_CHECK_BUTTON(w->overwrite_check)) ? 1 : 0;

    /* ----- output dir ----- */
    if (w->output_dir_path && w->output_dir_path[0] != '\0')
        g_strlcpy(opts->output_dir, w->output_dir_path, sizeof(opts->output_dir));
    else
        g_strlcpy(opts->output_dir, "", sizeof(opts->output_dir));

    /* ----- file list ----- */
    *out_count = w->file_paths->len;
    *out_files = g_malloc0(sizeof(char*) * (*out_count));
    for (int i = 0; i < *out_count; ++i) {
        *(*out_files + i) = g_strdup(g_ptr_array_index(w->file_paths, i));
    }
}

void clear_file_list(AppWidgets *w)
{
    GtkWidget *child = gtk_widget_get_first_child(w->file_listbox);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(w->file_listbox), child);
        child = next;
    }

    g_ptr_array_set_size(w->file_paths, 0);
}

static void set_output_dir(AppWidgets *w, const char *path)
{
    char *resolved = NULL;
    if (path && path[0] != '\0') {
        resolved = g_strdup(path);
    } else {
        const char *home = g_get_home_dir();
        resolved = g_build_filename(home, "ffmpeg_converter", NULL);
    }

    g_free(w->output_dir_path);
    w->output_dir_path = resolved;
    gtk_label_set_text(GTK_LABEL(w->output_dir_label), w->output_dir_path);
}

static char *get_dropdown_text(GtkWidget *dropdown)
{
    GObject *item = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(dropdown));
    if (!item)
        return g_strdup("");

    const char *text = gtk_string_object_get_string(GTK_STRING_OBJECT(item));
    char *copy = g_strdup(text ? text : "");
    return copy;
}
