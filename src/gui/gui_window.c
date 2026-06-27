/*  gui_window.c
 *  Build the GTK4 UI and manage user interaction.
 */

#include "gui_window.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <glib.h>
#include <gio/gio.h>
/* GTK4 main header includes all necessary types and functions */
#include <gtk/gtk.h>

/* Forward declarations */
static void update_dependent_widgets(AppWidgets *w);
static void on_codec_changed(GtkComboBox *obj, AppWidgets *w);
static void on_audio_norm_changed(GtkComboBox *obj, AppWidgets *w);
static gboolean update_dependent_widgets_idle(gpointer data);
static void schedule_update_dependent_widgets(AppWidgets *w);
static void on_add_files_clicked(GtkButton *button, AppWidgets *w);
static void on_add_files_response(GObject *source, GAsyncResult *res, AppWidgets *w);
static void on_add_track_clicked(GtkButton *button, AppWidgets *w);
static void on_apple_m4v_clicked(GtkButton *button, AppWidgets *w);
static void on_output_dir_clicked(GtkButton *button, AppWidgets *w);
static void on_output_dir_response(GObject *source, GAsyncResult *res, AppWidgets *w);
static void on_remove_file_clicked(GtkButton *button, AppWidgets *w);
static void on_clear_list_clicked(GtkButton *button, AppWidgets *w);
static void on_start_clicked(GtkButton *button, AppWidgets *w);
static void on_stop_clicked(GtkButton *button, AppWidgets *w);
static void set_output_dir(AppWidgets *w, const char *path);
static void set_video_track(AppWidgets *w, const char *path);
static char *get_dropdown_text(GtkWidget *dropdown);
static gboolean prompt_m4v_options(AppWidgets *w, M4VOptions *opts);
static void populate_codec_combo(AppWidgets *w);
static gboolean codec_uses_software_prores(const char *codec);
static gboolean codec_uses_linux_vaapi(const char *codec);
static gboolean codec_uses_vulkan_prores(const char *codec);
static gboolean codec_is_mux(const char *codec);
static void populate_vulkan_device_combo(AppWidgets *w);
static int get_selected_vulkan_device_index(AppWidgets *w);

static gboolean codec_uses_software_prores(const char *codec)
{
    return g_strcmp0(codec, "prores") == 0 ||
           g_strcmp0(codec, "prores_ks") == 0;
}

static gboolean codec_uses_linux_vaapi(const char *codec)
{
    return g_strcmp0(codec, "h264_vaapi") == 0 ||
           g_strcmp0(codec, "hevc_vaapi") == 0;
}

static gboolean codec_uses_vulkan_prores(const char *codec)
{
    return g_strcmp0(codec, "prores_ks_vulkan") == 0;
}

static gboolean codec_is_mux(const char *codec)
{
    return g_strcmp0(codec, "mux") == 0;
}

static void populate_vulkan_device_combo(AppWidgets *w)
{
    int i;
    int added = 0;
    char auto_label[64];

    if (!w || !w->vulkan_device_combo)
        return;

    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(w->vulkan_device_combo));

    if (w->linux_codec_support.vulkan_device_index >= 0) {
        snprintf(auto_label,
                 sizeof(auto_label),
                 "auto (recommended: vk:%d)",
                 w->linux_codec_support.vulkan_device_index);
    } else {
        g_strlcpy(auto_label, "auto", sizeof(auto_label));
    }
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w->vulkan_device_combo), "auto", auto_label);

    for (i = 0; i < 32; i++) {
        if ((((unsigned int)w->linux_codec_support.vulkan_working_mask) & (1u << i)) != 0u) {
            char id[16];
            char label[32];
            snprintf(id, sizeof(id), "%d", i);
            snprintf(label, sizeof(label), "vk:%d", i);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w->vulkan_device_combo), id, label);
            added++;
        }
    }

    if (added == 0 && w->linux_codec_support.vulkan_device_index >= 0) {
        char id[16];
        char label[32];
        snprintf(id, sizeof(id), "%d", w->linux_codec_support.vulkan_device_index);
        snprintf(label, sizeof(label), "vk:%d", w->linux_codec_support.vulkan_device_index);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w->vulkan_device_combo), id, label);
    }

    gtk_combo_box_set_active_id(GTK_COMBO_BOX(w->vulkan_device_combo), "auto");
}

static int get_selected_vulkan_device_index(AppWidgets *w)
{
    const char *id;
    char *endptr = NULL;
    long parsed;

    if (!w || !w->vulkan_device_combo)
        return -1;

    id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(w->vulkan_device_combo));
    if (!id || g_strcmp0(id, "auto") == 0)
        return -1;

    parsed = strtol(id, &endptr, 10);
    if (!endptr || *endptr != '\0' || parsed < 0 || parsed > INT_MAX)
        return -1;

    return (int)parsed;
}

static void populate_codec_combo(AppWidgets *w)
{
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->codec_combo), "copy");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->codec_combo), "prores");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->codec_combo), "prores_ks");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->codec_combo), "mux");

    if (w->linux_codec_support.has_h264_vaapi)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->codec_combo), "h264_vaapi");
    if (w->linux_codec_support.has_hevc_vaapi)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->codec_combo), "hevc_vaapi");
    if (w->linux_codec_support.has_h264_nvenc)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->codec_combo), "h264_nvenc");
    if (w->linux_codec_support.has_hevc_nvenc)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->codec_combo), "hevc_nvenc");
    if (w->linux_codec_support.has_h264_amf)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->codec_combo), "h264_amf");
    if (w->linux_codec_support.has_hevc_amf)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->codec_combo), "hevc_amf");
    if (w->linux_codec_support.has_h264_qsv)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->codec_combo), "h264_qsv");
    if (w->linux_codec_support.has_hevc_qsv)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->codec_combo), "hevc_qsv");
    if (w->linux_codec_support.has_prores_ks_vulkan)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->codec_combo), "prores_ks_vulkan");

    gtk_combo_box_set_active(GTK_COMBO_BOX(w->codec_combo), 0);
}

void set_running_ui_state(AppWidgets *w, gboolean running)
{
    if (!w || w->shutting_down)
        return;

    gtk_widget_set_sensitive(w->start_btn, !running);
    gtk_widget_set_sensitive(w->stop_btn, running);

    gtk_widget_set_sensitive(w->codec_combo, !running);
    gtk_widget_set_sensitive(w->vulkan_device_combo, !running);
    gtk_widget_set_sensitive(w->audio_norm_combo, !running);
    gtk_widget_set_sensitive(w->audio_output_combo, !running);
    gtk_widget_set_sensitive(w->overwrite_check, !running);
    gtk_widget_set_sensitive(w->output_dir_btn, !running);
    gtk_widget_set_sensitive(w->add_files_btn, !running);
    gtk_widget_set_sensitive(w->add_track_btn, !running);
    gtk_widget_set_sensitive(w->apple_m4v_btn, !running);
    gtk_widget_set_sensitive(w->remove_file_btn, !running);
    gtk_widget_set_sensitive(w->clear_list_btn, !running);
    gtk_widget_set_sensitive(w->file_listbox, !running);

    if (running) {
        gtk_widget_set_sensitive(w->profile_combo, FALSE);
        gtk_widget_set_sensitive(w->deblock_combo, FALSE);
        gtk_widget_set_sensitive(w->genre_combo, FALSE);
        return;
    }

    update_dependent_widgets(w);
}

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
        w->codec_combo = gtk_combo_box_text_new();
        populate_codec_combo(w);
        g_signal_connect(w->codec_combo, "changed", G_CALLBACK(on_codec_changed), w);
    }

    /* ---------- Vulkan device selector ---------- */
    w->vulkan_device_label = gtk_label_new("Vulkan dev:");
    gtk_widget_set_halign(w->vulkan_device_label, GTK_ALIGN_END);
    w->vulkan_device_combo = gtk_combo_box_text_new();
    populate_vulkan_device_combo(w);
    gtk_widget_set_visible(w->vulkan_device_label, FALSE);
    gtk_widget_set_visible(w->vulkan_device_combo, FALSE);

    /* ---------- Profile combo ---------- */
    {
        w->profile_combo = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->profile_combo), "lt");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->profile_combo), "standard");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->profile_combo), "hq");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->profile_combo), "4444");
        gtk_combo_box_set_active(GTK_COMBO_BOX(w->profile_combo), 1);
    }
    /* Initially disabled for copy and hardware codecs */
    gtk_widget_set_sensitive(w->profile_combo, FALSE);

    /* ---------- Deblock combo ---------- */
    {
        w->deblock_combo = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->deblock_combo), "none");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->deblock_combo), "weak");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->deblock_combo), "strong");
        gtk_combo_box_set_active(GTK_COMBO_BOX(w->deblock_combo), 0);
    }
    gtk_widget_set_sensitive(w->deblock_combo, FALSE);

    /* ---------- Audio norm combo ---------- */
    {
        w->audio_norm_combo = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->audio_norm_combo), "none");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->audio_norm_combo), "peak_norm");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->audio_norm_combo), "peak_norm_2pass");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->audio_norm_combo), "loudness_norm");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->audio_norm_combo), "loudness_norm_2pass");
        gtk_combo_box_set_active(GTK_COMBO_BOX(w->audio_norm_combo), 0);
        g_signal_connect(w->audio_norm_combo, "changed", G_CALLBACK(on_audio_norm_changed), w);
    }

    /* ---------- Genre combo ---------- */
    {
        w->genre_combo = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->genre_combo), "edm");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->genre_combo), "rock");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->genre_combo), "hiphop");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->genre_combo), "classical");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->genre_combo), "podcast");
        gtk_combo_box_set_active(GTK_COMBO_BOX(w->genre_combo), 0);
    }
    gtk_widget_set_sensitive(w->genre_combo, FALSE);

    /* ---------- Audio output combo ---------- */
    {
        w->audio_output_combo = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->audio_output_combo), "pcm");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->audio_output_combo), "fdk_aac_320");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->audio_output_combo), "fdk_aac_320_ac3_640");
        gtk_combo_box_set_active(GTK_COMBO_BOX(w->audio_output_combo), 0);
    }

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

    w->video_track_label = gtk_label_new("(not set)");
    gtk_label_set_xalign(GTK_LABEL(w->video_track_label), 0.0f);
    gtk_widget_set_hexpand(w->video_track_label, TRUE);
    w->add_track_btn = gtk_button_new_with_label("Add track...");
    gtk_widget_set_sensitive(w->add_track_btn, FALSE);
    g_signal_connect(w->add_track_btn, "clicked", G_CALLBACK(on_add_track_clicked), w);
    w->video_track_path = NULL;
    set_video_track(w, NULL);

    /* ---------- File list ---------- */
    w->file_listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(w->file_listbox), GTK_SELECTION_SINGLE);
    w->file_paths = g_ptr_array_new_with_free_func((GDestroyNotify)g_free);

    /* ---------- Buttons ---------- */
    w->add_files_btn = gtk_button_new_with_label("Add files...");
    g_signal_connect(w->add_files_btn, "clicked", G_CALLBACK(on_add_files_clicked), w);

    w->apple_m4v_btn = gtk_button_new_with_label("Apple m4v...");
    gtk_widget_set_sensitive(w->apple_m4v_btn, FALSE);
    g_signal_connect(w->apple_m4v_btn, "clicked", G_CALLBACK(on_apple_m4v_clicked), w);

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

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Audio out:"), 4, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->audio_output_combo, 5, r, 1, 1);
    r++;

    gtk_grid_attach(GTK_GRID(grid), w->vulkan_device_label, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->vulkan_device_combo, 1, r, 2, 1);
    r++;

    gtk_grid_attach(GTK_GRID(grid), w->overwrite_check, 0, r, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Output dir:"), 2, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->output_dir_label, 3, r, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), w->output_dir_btn, 5, r, 1, 1);
    r++;

    gtk_grid_attach(GTK_GRID(grid), w->add_files_btn, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->remove_file_btn, 1, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->clear_list_btn, 2, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->add_track_btn, 3, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->apple_m4v_btn, 4, r, 1, 1);
    r++;

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Video track:"), 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->video_track_label, 1, r, 5, 1);
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

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_child(GTK_WINDOW(window), grid);

    /* Return the window widget */
    return window;
}

/* ------------------------------------------------------------------ */
/* Update visibility / sensitivity of dependent widgets                */
/* ------------------------------------------------------------------ */
static void update_dependent_widgets(AppWidgets *w)
{
    if (!w || w->shutting_down)
        return;

    char *codec = get_dropdown_text(w->codec_combo);

    /* Profile & Deblock only for software ProRes */
    gboolean profile_sensitive = codec_uses_software_prores(codec);
    gtk_widget_set_sensitive(w->profile_combo, profile_sensitive);
    gtk_widget_set_sensitive(w->deblock_combo, profile_sensitive);

    gtk_widget_set_sensitive(w->add_files_btn,
                             !codec_is_mux(codec));
    gtk_widget_set_sensitive(w->add_track_btn,
                             codec_is_mux(codec) && w->file_paths->len == 1);
    gtk_widget_set_sensitive(w->apple_m4v_btn,
                             w->file_paths->len > 0);

    /* Genre only when audio_norm is loudness normalization 2-pass */
    char *audio_norm = get_dropdown_text(w->audio_norm_combo);
    gboolean genre_sensitive = g_strcmp0(audio_norm, "loudness_norm_2pass") == 0;
    gtk_widget_set_sensitive(w->genre_combo, genre_sensitive);

    {
        gboolean show_vulkan_device =
            codec_uses_vulkan_prores(codec) &&
            w->linux_codec_support.has_prores_ks_vulkan;
        gtk_widget_set_visible(w->vulkan_device_label, show_vulkan_device);
        gtk_widget_set_visible(w->vulkan_device_combo, show_vulkan_device);
    }

    g_free(audio_norm);
    g_free(codec);
}

static gboolean update_dependent_widgets_idle(gpointer data)
{
    AppWidgets *w = (AppWidgets *)data;
    if (!w)
        return G_SOURCE_REMOVE;

    w->dependent_update_source_id = 0;
    update_dependent_widgets(w);
    return G_SOURCE_REMOVE;
}

static void schedule_update_dependent_widgets(AppWidgets *w)
{
    if (!w || w->shutting_down)
        return;

    if (w->dependent_update_source_id != 0)
        return;

    w->dependent_update_source_id =
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                        update_dependent_widgets_idle,
                        w,
                        NULL);
}

/* ------------------------------------------------------------------ */
/* Callback: codec combo changed                                       */
/* ------------------------------------------------------------------ */
static void on_codec_changed(GtkComboBox *obj, AppWidgets *w)
{
    (void)obj;
    schedule_update_dependent_widgets(w);
}

/* ------------------------------------------------------------------ */
/* Callback: audio_norm combo changed                                   */
/* ------------------------------------------------------------------ */
static void on_audio_norm_changed(GtkComboBox *obj, AppWidgets *w)
{
    (void)obj;
    schedule_update_dependent_widgets(w);
}

/* ------------------------------------------------------------------ */
/* Add files button                                                    */
/* ------------------------------------------------------------------ */
static void on_file_chooser_response(GtkDialog *dialog, gint response_id, AppWidgets *w)
{
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        GListModel *files = gtk_file_chooser_get_files(chooser);
        
        guint n_items = g_list_model_get_n_items(files);
        for (guint i = 0; i < n_items; ++i) {
            GFile *file = (GFile *)g_list_model_get_item(files, i);
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
        schedule_update_dependent_widgets(w);
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void on_track_chooser_response(GtkDialog *dialog, gint response_id, AppWidgets *w)
{
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        GFile *file = gtk_file_chooser_get_file(chooser);
        if (file) {
            char *path = g_file_get_path(file);
            if (path)
                set_video_track(w, path);
            g_free(path);
            g_object_unref(file);
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void on_add_track_clicked(GtkButton *button, AppWidgets *w)
{
    GtkWidget *dialog;

    (void)button;
    dialog = gtk_file_chooser_dialog_new(
        "Select Video Track",
        GTK_WINDOW(w->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL
    );

    if (w->video_track_path && w->video_track_path[0] != '\0') {
        GFile *current = g_file_new_for_path(w->video_track_path);
        gtk_file_chooser_set_file(GTK_FILE_CHOOSER(dialog), current, NULL);
        g_object_unref(current);
    }

    g_signal_connect(dialog, "response", G_CALLBACK(on_track_chooser_response), w);
    gtk_widget_show(dialog);
}

typedef struct {
    GMainLoop *loop;
    M4VOptions *opts;
    gboolean accepted;
    GtkWidget *video_spin;
    GtkWidget *audio_spin;
    GtkWidget *ac3_spin;
    GtkWidget *chapters_check;
    GtkWidget *lang_entry;
} M4VDialogData;

static void on_m4v_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    M4VDialogData *data = (M4VDialogData *)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        const char *lang;
        data->opts->video_track_index = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(data->video_spin));
        data->opts->audio_track_index = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(data->audio_spin));
        data->opts->ac3_bitrate_kbps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(data->ac3_spin));
        data->opts->add_chapters = gtk_check_button_get_active(GTK_CHECK_BUTTON(data->chapters_check));

        lang = gtk_editable_get_text(GTK_EDITABLE(data->lang_entry));
        if (!lang || lang[0] == '\0')
            lang = "rus";
        g_strlcpy(data->opts->audio_lang, lang, sizeof(data->opts->audio_lang));
        data->accepted = TRUE;
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    g_main_loop_quit(data->loop);
}

static gboolean prompt_m4v_options(AppWidgets *w, M4VOptions *opts)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *grid;
    GtkWidget *video_spin;
    GtkWidget *audio_spin;
    GtkWidget *ac3_spin;
    GtkWidget *chapters_check;
    GtkWidget *lang_entry;
    GMainLoop *loop;
    M4VDialogData data;

    dialog = gtk_dialog_new_with_buttons("Apple m4v creator options",
                                         GTK_WINDOW(w->window),
                                         GTK_DIALOG_MODAL,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Start", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);

    video_spin = gtk_spin_button_new_with_range(0, 16, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(video_spin), opts->video_track_index);
    audio_spin = gtk_spin_button_new_with_range(0, 16, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(audio_spin), opts->audio_track_index);
    ac3_spin = gtk_spin_button_new_with_range(96, 1536, 32);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ac3_spin), opts->ac3_bitrate_kbps);
    chapters_check = gtk_check_button_new_with_label("Add chapters");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(chapters_check), opts->add_chapters != 0);
    lang_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(lang_entry), opts->audio_lang[0] != '\0' ? opts->audio_lang : "rus");

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Video track index:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), video_spin, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Audio track index:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), audio_spin, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("AC3 bitrate kbps:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ac3_spin, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Audio language:"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lang_entry, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), chapters_check, 0, 4, 2, 1);

    gtk_box_append(GTK_BOX(content), grid);

    loop = g_main_loop_new(NULL, FALSE);
    memset(&data, 0, sizeof(data));
    data.loop = loop;
    data.opts = opts;
    data.video_spin = video_spin;
    data.audio_spin = audio_spin;
    data.ac3_spin = ac3_spin;
    data.chapters_check = chapters_check;
    data.lang_entry = lang_entry;

    g_signal_connect(dialog, "response", G_CALLBACK(on_m4v_dialog_response), &data);
    gtk_widget_show(dialog);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return data.accepted;
}

static void on_apple_m4v_clicked(GtkButton *button, AppWidgets *w)
{
    M4VOptions opts;

    (void)button;
    opts = w->pending_m4v_options;
    if (!prompt_m4v_options(w, &opts))
        return;

    w->pending_m4v_options = opts;
    start_m4v_creation(w);
}

static void on_add_files_clicked(GtkButton *button, AppWidgets *w)
{
    (void)button;
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Files",
        GTK_WINDOW(w->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
                                        g_file_new_for_path(g_get_home_dir()), NULL);
    
    g_signal_connect(dialog, "response", G_CALLBACK(on_file_chooser_response), w);
    gtk_widget_show(dialog);
}

static void on_add_files_response(GObject *source, GAsyncResult *res, AppWidgets *w)
{
    /* This function is deprecated, kept for compatibility */
    (void)source;
    (void)res;
    (void)w;
}

static void on_folder_chooser_response(GtkDialog *dialog, gint response_id, AppWidgets *w)
{
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        GFile *file = gtk_file_chooser_get_file(chooser);
        if (file) {
            char *path = g_file_get_path(file);
            if (path)
                set_output_dir(w, path);
            g_free(path);
            g_object_unref(file);
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void on_output_dir_clicked(GtkButton *button, AppWidgets *w)
{
    (void)button;
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Output Directory",
        GTK_WINDOW(w->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    if (w->output_dir_path && w->output_dir_path[0] != '\0') {
        GFile *current = g_file_new_for_path(w->output_dir_path);
        gtk_file_chooser_set_file(GTK_FILE_CHOOSER(dialog), current, NULL);
        g_object_unref(current);
    }
    
    g_signal_connect(dialog, "response", G_CALLBACK(on_folder_chooser_response), w);
    gtk_widget_show(dialog);
}

static void on_output_dir_response(GObject *source, GAsyncResult *res, AppWidgets *w)
{
    /* This function is deprecated, kept for compatibility */
    (void)source;
    (void)res;
    (void)w;
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
    schedule_update_dependent_widgets(w);
}

static void on_clear_list_clicked(GtkButton *button, AppWidgets *w)
{
    (void)button;
    clear_file_list(w);
    set_video_track(w, NULL);
    schedule_update_dependent_widgets(w);
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
    memset(opts, 0, sizeof(*opts));

    /* ----- codec ----- */
    char *codec = get_dropdown_text(w->codec_combo);
    g_strlcpy(opts->codec, codec ? codec : "", sizeof(opts->codec));
    g_free(codec);

    /* ----- profile ----- */
    if (gtk_widget_get_sensitive(w->profile_combo)) {
        int active = gtk_combo_box_get_active(GTK_COMBO_BOX(w->profile_combo));
        opts->profile = active >= 0 ? active + 1 : 0;
    } else {
        opts->profile = 0;
    }

    /* ----- deblock ----- */
    if (gtk_widget_get_sensitive(w->deblock_combo)) {
        int active = gtk_combo_box_get_active(GTK_COMBO_BOX(w->deblock_combo));
        opts->deblock = active >= 0 ? active + 1 : 0;
    } else {
        opts->deblock = 0;
    }

    /* ----- audio norm ----- */
    char *norm = get_dropdown_text(w->audio_norm_combo);
    g_strlcpy(opts->audio_norm, norm ? norm : "", sizeof(opts->audio_norm));
    g_free(norm);

    char *audio_output = get_dropdown_text(w->audio_output_combo);
    g_strlcpy(opts->audio_output_mode,
              audio_output ? audio_output : "pcm",
              sizeof(opts->audio_output_mode));
    g_free(audio_output);

    g_strlcpy(opts->video_track_path,
              w->video_track_path ? w->video_track_path : "",
              sizeof(opts->video_track_path));

    /* ----- genre ----- */
    if (gtk_widget_get_sensitive(w->genre_combo)) {
        int active = gtk_combo_box_get_active(GTK_COMBO_BOX(w->genre_combo));
        opts->genre = active >= 0 ? active + 1 : 0;
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

    if (codec_uses_linux_vaapi(opts->codec) && w->linux_codec_support.default_render_node[0] != '\0') {
        g_strlcpy(opts->hw_device,
                  w->linux_codec_support.default_render_node,
                  sizeof(opts->hw_device));
    }

    if (codec_uses_vulkan_prores(opts->codec)) {
        int selected_device = get_selected_vulkan_device_index(w);
        if (selected_device >= 0) {
            opts->vulkan_device = selected_device;
        } else {
            opts->vulkan_device = (w->linux_codec_support.vulkan_device_index >= 0)
                                      ? w->linux_codec_support.vulkan_device_index
                                      : 1;
        }
    } else {
        opts->vulkan_device = 0;
    }

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

static void set_video_track(AppWidgets *w, const char *path)
{
    g_free(w->video_track_path);
    w->video_track_path = g_strdup(path ? path : "");

    if (w->video_track_path[0] != '\0')
        gtk_label_set_text(GTK_LABEL(w->video_track_label), w->video_track_path);
    else
        gtk_label_set_text(GTK_LABEL(w->video_track_label), "(not set)");
}

static char *get_dropdown_text(GtkWidget *dropdown)
{
    char *text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dropdown));
    if (!text)
        return g_strdup("");
    return text;
}
