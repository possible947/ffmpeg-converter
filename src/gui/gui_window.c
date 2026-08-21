/*  gui_window.c
 *  Build the GTK4 UI and manage user interaction.
 */

#include "gui_window.h"
#include "gui_codec_utils.h"
#include "preset_loader.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <glib.h>
#include <gio/gio.h>
/* GTK4 main header includes all necessary types and functions */
#include <gtk/gtk.h>

/* Forward declarations */
static void update_dependent_widgets(AppWidgets *w);
static void on_codec_changed(GObject *obj, GParamSpec *pspec, AppWidgets *w);
static void on_audio_norm_changed(GObject *obj, GParamSpec *pspec, AppWidgets *w);
static gboolean update_dependent_widgets_idle(gpointer data);
static void schedule_update_dependent_widgets(AppWidgets *w);
static void on_add_files_clicked(GtkButton *button, AppWidgets *w);
static void on_add_files_finish(GObject *source, GAsyncResult *res, gpointer user_data);
static void on_add_track_clicked(GtkButton *button, AppWidgets *w);
static void on_add_track_finish(GObject *source, GAsyncResult *res, gpointer user_data);
static void on_apple_m4v_clicked(GtkButton *button, AppWidgets *w);
static void on_output_dir_clicked(GtkButton *button, AppWidgets *w);
static void on_output_dir_finish(GObject *source, GAsyncResult *res, gpointer user_data);
static void on_remove_file_clicked(GtkButton *button, AppWidgets *w);
static void on_clear_list_clicked(GtkButton *button, AppWidgets *w);
static void on_start_clicked(GtkButton *button, AppWidgets *w);
static void on_stop_clicked(GtkButton *button, AppWidgets *w);
static void set_output_dir(AppWidgets *w, const char *path);
static void set_video_track(AppWidgets *w, const char *path);
static void add_file_to_list(AppWidgets *w, const char *path);
static char *get_dropdown_text(GtkWidget *dropdown);
static void prompt_m4v_options_async(AppWidgets *w);
static void populate_codec_combo(AppWidgets *w);
static void populate_preset_combo(AppWidgets *w);
static void populate_vulkan_device_combo(AppWidgets *w);
static int get_selected_vulkan_device_index(AppWidgets *w);
static void populate_vaapi_device_combo(AppWidgets *w);
static void get_selected_vaapi_device(AppWidgets *w, char *out, size_t out_sz);
static void install_drop_target(AppWidgets *w);

/* Static preset database for dynamic preset loading */
static PresetDb *g_preset_db = NULL;

/* Helper to get platform name for preset queries */
static const char *get_platform_name(void)
{
#if defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

/* Selects the recommended device index and working-device bitmask for the
 * currently selected codec. Hardware Vulkan encoders (h264_vulkan/
 * hevc_vulkan/av1_vulkan) are probed independently of prores_ks_vulkan and
 * may only succeed on a different physical GPU, so each family's device
 * combo must be built from its own probe result. */
static void get_vulkan_probe_for_codec(AppWidgets *w, const char *codec,
                                       int *out_index, int *out_mask)
{
    if (codec_uses_hw_vulkan(codec)) {
        *out_index = w->linux_codec_support.vulkan_hw_device_index;
        *out_mask  = w->linux_codec_support.vulkan_hw_working_mask;
    } else {
        /* prores_ks_vulkan, or no vulkan codec selected yet (initial
         * population before the codec combo has a meaningful selection). */
        *out_index = w->linux_codec_support.vulkan_device_index;
        *out_mask  = w->linux_codec_support.vulkan_working_mask;
    }
}

static void populate_vulkan_device_combo(AppWidgets *w)
{
    int i;
    int added = 0;
    char auto_label[64];
    gint auto_id = -1;
    char *codec;
    int device_index = -1;
    int working_mask = 0;

    if (!w || !w->vulkan_device_combo)
        return;

    codec = get_dropdown_text(w->codec_combo);
    get_vulkan_probe_for_codec(w, codec, &device_index, &working_mask);
    g_free(codec);

    /* Clear both the string model and the parallel device-index array. */
    {
        guint n = g_list_model_get_n_items(G_LIST_MODEL(w->vulkan_device_list));
        if (n > 0)
            gtk_string_list_splice(w->vulkan_device_list, 0, n, NULL);
    }
    g_array_set_size(w->vulkan_device_ids, 0);

    /* "auto" entry — index 0, maps to device -1 (let ffmpeg decide). */
    if (device_index >= 0) {
        snprintf(auto_label, sizeof(auto_label),
                 "auto (recommended: vk:%d)", device_index);
    } else {
        g_strlcpy(auto_label, "auto", sizeof(auto_label));
    }
    gtk_string_list_append(w->vulkan_device_list, auto_label);
    g_array_append_val(w->vulkan_device_ids, auto_id);

    /* Entries for every working Vulkan device for this codec family. */
    for (i = 0; i < 32; i++) {
        if ((((unsigned int)working_mask) & (1u << i)) != 0u) {
            char label[32];
            gint dev = i;
            snprintf(label, sizeof(label), "vk:%d", i);
            gtk_string_list_append(w->vulkan_device_list, label);
            g_array_append_val(w->vulkan_device_ids, dev);
            added++;
        }
    }

    /* Fallback: mask empty but a recommended device is known. */
    if (added == 0 && device_index >= 0) {
        char label[32];
        gint dev = device_index;
        snprintf(label, sizeof(label), "vk:%d", dev);
        gtk_string_list_append(w->vulkan_device_list, label);
        g_array_append_val(w->vulkan_device_ids, dev);
    }

    gtk_drop_down_set_selected(GTK_DROP_DOWN(w->vulkan_device_combo), 0);
}

static int get_selected_vulkan_device_index(AppWidgets *w)
{
    guint sel;

    if (!w || !w->vulkan_device_combo || !w->vulkan_device_ids)
        return -1;

    sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->vulkan_device_combo));
    if (sel == GTK_INVALID_LIST_POSITION || sel >= w->vulkan_device_ids->len)
        return -1;

    return g_array_index(w->vulkan_device_ids, gint, sel);
}

static void populate_vaapi_device_combo(AppWidgets *w)
{
    const char *dir_path = "/dev/dri";
    DIR *dir;
    struct dirent *entry;
    char auto_label[1100];

    if (!w || !w->vaapi_device_combo)
        return;

    /* Clear the string model and the parallel render-node array. */
    {
        guint n = g_list_model_get_n_items(G_LIST_MODEL(w->vaapi_device_list));
        if (n > 0)
            gtk_string_list_splice(w->vaapi_device_list, 0, n, NULL);
    }
    if (w->vaapi_device_nodes) {
        guint i;
        for (i = 0; i < w->vaapi_device_nodes->len; i++)
            g_free(g_array_index(w->vaapi_device_nodes, gchar*, i));
        g_array_set_size(w->vaapi_device_nodes, 0);
    }

    /* "auto" entry — index 0, maps to "" (converter auto-selects the node). */
    if (w->linux_codec_support.default_render_node[0] != '\0') {
        snprintf(auto_label, sizeof(auto_label),
                 "auto (recommended: %s)",
                 w->linux_codec_support.default_render_node);
    } else {
        g_strlcpy(auto_label, "auto", sizeof(auto_label));
    }
    gtk_string_list_append(w->vaapi_device_list, auto_label);
    if (w->vaapi_device_nodes) {
        gchar *auto_node = g_strdup("");
        g_array_append_val(w->vaapi_device_nodes, auto_node);
    }

    /* One entry per usable render node. */
    dir = opendir(dir_path);
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            char node[PATH_MAX];
            char friendly_name[512];
            gchar *dup;
            if (strncmp(entry->d_name, "renderD", 7) != 0)
                continue;
            snprintf(node, sizeof(node), "%s/%s", dir_path, entry->d_name);
            if (access(node, R_OK | W_OK) != 0)
                continue;

            /* Get friendly device name (e.g., "Intel UHD Graphics (renderD128)") */
            if (linux_get_vaapi_device_name(node, friendly_name, sizeof(friendly_name)) == 0) {
                gtk_string_list_append(w->vaapi_device_list, friendly_name);
            } else {
                /* Fallback to full path if name lookup fails */
                gtk_string_list_append(w->vaapi_device_list, node);
            }

            dup = g_strdup(node);
            g_array_append_val(w->vaapi_device_nodes, dup);
        }
        closedir(dir);
    }

    gtk_drop_down_set_selected(GTK_DROP_DOWN(w->vaapi_device_combo), 0);
}

static void get_selected_vaapi_device(AppWidgets *w, char *out, size_t out_sz)
{
    guint sel;

    if (!out || out_sz == 0)
        return;
    out[0] = '\0';

    if (!w || !w->vaapi_device_combo || !w->vaapi_device_nodes)
        return;

    sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->vaapi_device_combo));
    if (sel == GTK_INVALID_LIST_POSITION || sel >= w->vaapi_device_nodes->len)
        return;

    g_strlcpy(out, g_array_index(w->vaapi_device_nodes, gchar*, sel), out_sz);
}

static void populate_codec_combo(AppWidgets *w)
{
    gtk_string_list_append(w->codec_list, "copy");
    gtk_string_list_append(w->codec_list, "prores");
    gtk_string_list_append(w->codec_list, "prores_ks");
    gtk_string_list_append(w->codec_list, "mux");

    if (w->linux_codec_support.has_h264_vaapi)
        gtk_string_list_append(w->codec_list, "h264_vaapi");
    if (w->linux_codec_support.has_hevc_vaapi)
        gtk_string_list_append(w->codec_list, "hevc_vaapi");
    if (w->linux_codec_support.has_h264_nvenc)
        gtk_string_list_append(w->codec_list, "h264_nvenc");
    if (w->linux_codec_support.has_hevc_nvenc)
        gtk_string_list_append(w->codec_list, "hevc_nvenc");
    if (w->linux_codec_support.has_h264_amf)
        gtk_string_list_append(w->codec_list, "h264_amf");
    if (w->linux_codec_support.has_hevc_amf)
        gtk_string_list_append(w->codec_list, "hevc_amf");
    if (w->linux_codec_support.has_av1_amf)
        gtk_string_list_append(w->codec_list, "av1_amf");
    if (w->linux_codec_support.has_h264_qsv)
        gtk_string_list_append(w->codec_list, "h264_qsv");
    if (w->linux_codec_support.has_hevc_qsv)
        gtk_string_list_append(w->codec_list, "hevc_qsv");
    if (w->linux_codec_support.has_prores_ks_vulkan)
        gtk_string_list_append(w->codec_list, "prores_ks_vulkan");
    if (w->linux_codec_support.has_h264_vulkan)
        gtk_string_list_append(w->codec_list, "h264_vulkan");
    if (w->linux_codec_support.has_hevc_vulkan)
        gtk_string_list_append(w->codec_list, "hevc_vulkan");
    if (w->linux_codec_support.has_av1_vulkan)
        gtk_string_list_append(w->codec_list, "av1_vulkan");

    gtk_drop_down_set_selected(GTK_DROP_DOWN(w->codec_combo), 0);
}

/* ------------------------------------------------------------------ */
/* Populate preset combo with presets for selected codec               */
/* ------------------------------------------------------------------ */

static void populate_preset_combo(AppWidgets *w)
{
    char *codec;
    const char **presets;
    int preset_count;
    int i;
    const char *platform;

    if (!w || !w->profile_combo || !w->preset_list)
        return;

    /* Get the currently selected codec */
    codec = get_dropdown_text(w->codec_combo);
    if (!codec || codec[0] == '\0') {
        g_free(codec);
        return;
    }

    /* Initialize preset database if not already done */
    if (!g_preset_db) {
        g_preset_db = preset_db_load(NULL);
        if (!g_preset_db) {
            g_free(codec);
            return;
        }
    }

    /* Clear existing presets */
    {
        guint n = g_list_model_get_n_items(G_LIST_MODEL(w->preset_list));
        for (guint j = 0; j < n; j++)
            gtk_string_list_remove(w->preset_list, 0);
    }

    /* Load presets from database */
    platform = get_platform_name();
    preset_count = preset_db_list_presets(g_preset_db, platform, codec, &presets);

    if (preset_count > 0) {
        for (i = 0; i < preset_count; i++) {
            gtk_string_list_append(w->preset_list, presets[i]);
        }
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->profile_combo), 0);
        free((void*)presets);
    }

    g_free(codec);
}

/* ------------------------------------------------------------------ */
/* Background hardware codec probe                                     */
/* ------------------------------------------------------------------ */

/* Idle callback: runs on the main thread after the probe finishes.
 * Re-populates the codec combo with the detected hardware entries. */
static gboolean on_probe_done(gpointer data)
{
    AppWidgets *w = (AppWidgets *)data;

    if (!w || w->shutting_down)
        return G_SOURCE_REMOVE;

    /* Rebuild the codec combo now that hardware info is available.
     * Block the notify::selected signal while we clear and re-fill. */
    g_signal_handlers_block_by_func(w->codec_combo, on_codec_changed, w);
    {
        guint n = g_list_model_get_n_items(G_LIST_MODEL(w->codec_list));
        if (n > 0)
            gtk_string_list_splice(w->codec_list, 0, n, NULL);
    }
    populate_codec_combo(w);
    g_signal_handlers_unblock_by_func(w->codec_combo, on_codec_changed, w);

    /* Also refresh the Vulkan device list now that probe data is ready. */
    populate_vulkan_device_combo(w);
    /* ... and the VAAPI render-node list. */
    populate_vaapi_device_combo(w);

    gtk_label_set_text(GTK_LABEL(w->status_label), "Ready");
    schedule_update_dependent_widgets(w);
    return G_SOURCE_REMOVE;
}

/* Thread function: runs the blocking hardware probe, then schedules
 * the idle callback to update the UI on the main thread. */
static gpointer run_hw_probe(gpointer data)
{
    AppWidgets *w = (AppWidgets *)data;
    linux_probe_codec_support(&w->linux_codec_support);
    g_idle_add(on_probe_done, w);
    return NULL;
}

/* Public entry point: launch the probe thread. Called from activate_cb(). */
void start_hw_probe(AppWidgets *w)
{
    w->probe_thread = g_thread_new("hw-probe", run_hw_probe, w);
}

void set_running_ui_state(AppWidgets *w, gboolean running)
{
    if (!w || w->shutting_down)
        return;

    gtk_widget_set_sensitive(w->start_btn, !running);
    gtk_widget_set_sensitive(w->stop_btn, running);

    gtk_widget_set_sensitive(w->codec_combo, !running);
    gtk_widget_set_sensitive(w->vulkan_device_combo, !running);
    gtk_widget_set_sensitive(w->vaapi_device_combo, !running);
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
        /* The list is stored in AppWidgets so on_probe_done can append
         * hardware codec entries after the probe thread finishes. */
        w->codec_list  = gtk_string_list_new(NULL);
        w->codec_combo = gtk_drop_down_new(G_LIST_MODEL(w->codec_list), NULL);
        populate_codec_combo(w);
        g_signal_connect(w->codec_combo, "notify::selected",
                         G_CALLBACK(on_codec_changed), w);
        gtk_widget_set_hexpand(w->codec_combo, TRUE);
    }

    /* ---------- Vulkan device selector ---------- */
    w->vulkan_device_label = gtk_label_new("Vulkan dev:");
    gtk_widget_set_halign(w->vulkan_device_label, GTK_ALIGN_END);
    {
        w->vulkan_device_ids  = g_array_new(FALSE, TRUE, sizeof(gint));
        w->vulkan_device_list = gtk_string_list_new(NULL);
        w->vulkan_device_combo = gtk_drop_down_new(G_LIST_MODEL(w->vulkan_device_list), NULL);
        populate_vulkan_device_combo(w);
        gtk_widget_set_hexpand(w->vulkan_device_combo, TRUE);
    }
    gtk_widget_set_visible(w->vulkan_device_label, FALSE);
    gtk_widget_set_visible(w->vulkan_device_combo, FALSE);

    /* ---------- VAAPI device selector ---------- */
    w->vaapi_device_label = gtk_label_new("VAAPI dev:");
    gtk_widget_set_halign(w->vaapi_device_label, GTK_ALIGN_END);
    {
        w->vaapi_device_nodes = g_array_new(FALSE, TRUE, sizeof(gchar*));
        w->vaapi_device_list  = gtk_string_list_new(NULL);
        w->vaapi_device_combo = gtk_drop_down_new(G_LIST_MODEL(w->vaapi_device_list), NULL);
        populate_vaapi_device_combo(w);
        gtk_widget_set_hexpand(w->vaapi_device_combo, TRUE);
    }
    gtk_widget_set_visible(w->vaapi_device_label, FALSE);
    gtk_widget_set_visible(w->vaapi_device_combo, FALSE);

    /* ---------- Profile combo ---------- */
    {
        w->preset_list = gtk_string_list_new(NULL);
        w->profile_combo = gtk_drop_down_new(G_LIST_MODEL(w->preset_list), NULL);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->profile_combo), 0); /* default: first preset */
        gtk_widget_set_hexpand(w->profile_combo, TRUE);
    }
    /* Initially disabled for copy and hardware codecs */
    gtk_widget_set_sensitive(w->profile_combo, FALSE);

    /* ---------- Deblock combo ---------- */
    {
        static const char *deblock_items[] = {"none", "weak", "strong", NULL};
        GtkStringList *list = gtk_string_list_new(deblock_items);
        w->deblock_combo = gtk_drop_down_new(G_LIST_MODEL(list), NULL);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->deblock_combo), 0);
        g_object_unref(list);
        gtk_widget_set_hexpand(w->deblock_combo, TRUE);
    }
    gtk_widget_set_sensitive(w->deblock_combo, FALSE);

    /* ---------- Audio norm combo ---------- */
    {
        static const char *norm_items[] = {
            "none", "peak_norm", "peak_norm_2pass",
            "loudness_norm", "loudness_norm_2pass", NULL
        };
        GtkStringList *list = gtk_string_list_new(norm_items);
        w->audio_norm_combo = gtk_drop_down_new(G_LIST_MODEL(list), NULL);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->audio_norm_combo), 0);
        g_signal_connect(w->audio_norm_combo, "notify::selected",
                         G_CALLBACK(on_audio_norm_changed), w);
        g_object_unref(list);
        gtk_widget_set_hexpand(w->audio_norm_combo, TRUE);
    }

    /* ---------- Genre combo ---------- */
    {
        static const char *genre_items[] = {
            "edm", "rock", "hiphop", "classical", "podcast", NULL
        };
        GtkStringList *list = gtk_string_list_new(genre_items);
        w->genre_combo = gtk_drop_down_new(G_LIST_MODEL(list), NULL);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->genre_combo), 0);
        g_object_unref(list);
        gtk_widget_set_hexpand(w->genre_combo, TRUE);
    }
    gtk_widget_set_sensitive(w->genre_combo, FALSE);

    /* ---------- Audio output combo ---------- */
    {
        static const char *output_items[] = {
            "pcm", "fdk_aac_320", "fdk_aac_320_ac3_640", NULL
        };
        GtkStringList *list = gtk_string_list_new(output_items);
        w->audio_output_combo = gtk_drop_down_new(G_LIST_MODEL(list), NULL);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->audio_output_combo), 0);
        g_object_unref(list);
        gtk_widget_set_hexpand(w->audio_output_combo, TRUE);
    }

    /* ---------- Overwrite check ---------- */
    w->overwrite_check = gtk_check_button_new_with_label("Overwrite existing files");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(w->overwrite_check), FALSE);

    /* ---------- Output directory ---------- */
    w->output_dir_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(w->output_dir_label), 0.0f);
    gtk_widget_set_hexpand(w->output_dir_label, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(w->output_dir_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(w->output_dir_label), 50);
    w->output_dir_btn = gtk_button_new_with_label("Choose...");
    g_signal_connect(w->output_dir_btn, "clicked", G_CALLBACK(on_output_dir_clicked), w);
    w->output_dir_path = NULL;
    set_output_dir(w, NULL);

    w->video_track_label = gtk_label_new("(not set)");
    gtk_label_set_xalign(GTK_LABEL(w->video_track_label), 0.0f);
    gtk_widget_set_hexpand(w->video_track_label, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(w->video_track_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(w->video_track_label), 50);
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
    gtk_widget_add_css_class(w->log_view, "log");
    w->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->log_view));
    {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(w->log_buffer, &end);
        w->log_end_mark = gtk_text_buffer_create_mark(w->log_buffer, "log_end", &end, FALSE);
    }

    GtkWidget *log_scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scroller), w->log_view);
    gtk_widget_set_vexpand(log_scroller, TRUE);
    gtk_widget_set_size_request(log_scroller, -1, 140);

    /* ---------- Status line ---------- */
#if defined(__linux__)
    w->status_label = gtk_label_new("Detecting hardware encoders...");
#else
    w->status_label = gtk_label_new("Ready");
#endif
    gtk_label_set_xalign(GTK_LABEL(w->status_label), 0.0f);
    gtk_widget_set_hexpand(w->status_label, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(w->status_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(w->status_label), 80);

    /* ---------- Tooltips ---------- */
    gtk_widget_set_tooltip_text(w->codec_combo,
        "Video codec. Hardware codecs (VAAPI, Vulkan) are detected at startup.");
    gtk_widget_set_tooltip_text(w->profile_combo,
        "ProRes profile: lt (low bitrate), standard, hq (high quality), 4444.");
    gtk_widget_set_tooltip_text(w->deblock_combo,
        "Deblock filter strength applied during encoding.");
    gtk_widget_set_tooltip_text(w->audio_norm_combo,
        "Audio normalisation mode. peak_norm clips to 0 dBFS; loudness_norm targets EBU R128.");
    gtk_widget_set_tooltip_text(w->genre_combo,
        "Genre hint used by loudness normalisation to target genre-appropriate loudness.");
    gtk_widget_set_tooltip_text(w->audio_output_combo,
        "Audio output format: PCM (uncompressed), AAC 320 kbps, or AAC+AC3 dual track.");
    gtk_widget_set_tooltip_text(w->overwrite_check,
        "Overwrite output files if they already exist.");
    gtk_widget_set_tooltip_text(w->output_dir_btn,
        "Choose the directory where converted files will be saved.");
    gtk_widget_set_tooltip_text(w->add_files_btn,
        "Add video files to the conversion queue (Ctrl+O).");
    gtk_widget_set_tooltip_text(w->remove_file_btn,
        "Remove the selected file from the queue (Delete).");
    gtk_widget_set_tooltip_text(w->clear_list_btn,
        "Clear all files from the queue (Ctrl+L).");
    gtk_widget_set_tooltip_text(w->add_track_btn,
        "Select a video track file for Mux mode (MUX codec only).");
    gtk_widget_set_tooltip_text(w->apple_m4v_btn,
        "Create an Apple M4V file with AAC + AC3 audio from the queued files.");
    gtk_widget_set_tooltip_text(w->start_btn,
        "Start conversion of all queued files (Ctrl+Return).");
    gtk_widget_set_tooltip_text(w->stop_btn,
        "Stop the current conversion (Escape).");

    /* ---------- Layout ---------- */
    int r = 0;

    /* — Video section — */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Codec:"), 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->codec_combo, 1, r, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Profile:"), 2, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->profile_combo, 3, r, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Deblock:"), 4, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->deblock_combo, 5, r, 1, 1);
    r++;

    /* — Audio section — */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Audio norm:"), 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->audio_norm_combo, 1, r, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Genre:"), 2, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->genre_combo, 3, r, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Audio out:"), 4, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->audio_output_combo, 5, r, 1, 1);
    r++;

    /* Vulkan device row (hidden unless a Vulkan-capable codec is selected) */
    gtk_grid_attach(GTK_GRID(grid), w->vulkan_device_label, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->vulkan_device_combo, 1, r, 2, 1);
    r++;

    /* VAAPI device row (hidden unless a VAAPI codec is selected) */
    gtk_grid_attach(GTK_GRID(grid), w->vaapi_device_label, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->vaapi_device_combo, 1, r, 2, 1);
    r++;

    /* — Separator — */
    {
        GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_set_margin_top(sep, 2);
        gtk_widget_set_margin_bottom(sep, 2);
        gtk_grid_attach(GTK_GRID(grid), sep, 0, r, 6, 1);
        r++;
    }

    /* — Output section — */
    gtk_grid_attach(GTK_GRID(grid), w->overwrite_check, 0, r, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Output dir:"), 2, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->output_dir_label, 3, r, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), w->output_dir_btn, 5, r, 1, 1);
    r++;

    /* — Separator — */
    {
        GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_set_margin_top(sep, 2);
        gtk_widget_set_margin_bottom(sep, 2);
        gtk_grid_attach(GTK_GRID(grid), sep, 0, r, 6, 1);
        r++;
    }

    /* — Files section: action buttons — */
    gtk_grid_attach(GTK_GRID(grid), w->add_files_btn, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->remove_file_btn, 1, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->clear_list_btn, 2, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->add_track_btn, 3, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->apple_m4v_btn, 4, r, 1, 1);
    r++;

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Video track:"), 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->video_track_label, 1, r, 5, 1);
    r++;

    /* — File list + log: GtkPaned so the user can resize the split — */
    GtkWidget *file_scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(file_scroller), w->file_listbox);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(file_scroller),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(file_scroller, -1, 100);

    gtk_widget_set_size_request(log_scroller, -1, 100);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_paned_set_start_child(GTK_PANED(paned), file_scroller);
    gtk_paned_set_end_child(GTK_PANED(paned), log_scroller);
    gtk_paned_set_position(GTK_PANED(paned), 200);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);

    gtk_grid_attach(GTK_GRID(grid), paned, 0, r, 6, 1);
    r++;

    /* — Progress section — */
    gtk_grid_attach(GTK_GRID(grid), w->start_btn, 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->stop_btn, 1, r, 1, 1);
    r++;

    gtk_grid_attach(GTK_GRID(grid), w->progress_bar, 0, r, 6, 1);
    r++;

    gtk_grid_attach(GTK_GRID(grid), w->status_label, 0, r, 6, 1);
    r++;

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_child(GTK_WINDOW(window), grid);

    /* Set w->window here so install_drop_target() can reference it;
     * activate_cb() also assigns the returned value — both are the same pointer. */
    w->window = window;
    install_drop_target(w);

    /* Load application CSS:
     *   - log view: monospace font, works in both light and dark themes.
     *   - drag-hover: highlight using rgba values that are neutral across themes.
     */
    {
        GtkCssProvider *css = gtk_css_provider_new();
        gtk_css_provider_load_from_string(css,
            /* Monospace log view */
            "textview.log {"
            "  font-family: monospace;"
            "  font-size: 9pt;"
            "}"
            /* File list drag-and-drop hover highlight */
            "listbox.drag-hover {"
            "  background-color: rgba(53,132,228,0.12);"
            "  border: 2px dashed rgba(53,132,228,0.75);"
            "  border-radius: 6px;"
            "}");
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(css),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);
    }

    /* Return the window widget */
    return window;
}

/* ------------------------------------------------------------------ */
/* Drag-and-drop support                                               */
/* ------------------------------------------------------------------ */

static gboolean on_drop_files(GtkDropTarget *target, const GValue *value,
                               double x, double y, gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;
    GdkFileList *file_list;
    GSList *files;
    GSList *l;

    (void)target; (void)x; (void)y;

    file_list = g_value_get_boxed(value);
    if (!file_list)
        return FALSE;

    files = gdk_file_list_get_files(file_list);
    for (l = files; l; l = l->next) {
        char *path = g_file_get_path(G_FILE(l->data));
        add_file_to_list(w, path);
        g_free(path);
    }

    gtk_widget_remove_css_class(w->file_listbox, "drag-hover");
    schedule_update_dependent_widgets(w);
    return TRUE;
}

static GdkDragAction on_drop_enter(GtkDropTarget *target, double x, double y,
                                    gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;
    (void)target; (void)x; (void)y;
    gtk_widget_add_css_class(w->file_listbox, "drag-hover");
    return GDK_ACTION_COPY;
}

static void on_drop_leave(GtkDropTarget *target, gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;
    (void)target;
    gtk_widget_remove_css_class(w->file_listbox, "drag-hover");
}

/* Install a GtkDropTarget on the main window so files dropped anywhere
 * on the window are added to the queue. */
static void install_drop_target(AppWidgets *w)
{
    GtkDropTarget *target = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect(target, "drop",  G_CALLBACK(on_drop_files), w);
    g_signal_connect(target, "enter", G_CALLBACK(on_drop_enter), w);
    g_signal_connect(target, "leave", G_CALLBACK(on_drop_leave), w);
    gtk_widget_add_controller(w->window, GTK_EVENT_CONTROLLER(target));
}

/* ------------------------------------------------------------------ */
/* Update visibility / sensitivity of dependent widgets                */
/* ------------------------------------------------------------------ */
static void update_dependent_widgets(AppWidgets *w)
{
    if (!w || w->shutting_down)
        return;

    char *codec = get_dropdown_text(w->codec_combo);

    /* Update preset combo with presets for the selected codec */
    populate_preset_combo(w);

    /* Profile: software ProRes and Vulkan ProRes (profile:v mapping).
     * Deblock: software ProRes encoders only; hardware encoders skip it. */
    gboolean profile_sensitive = codec_uses_software_prores(codec) ||
                                 codec_uses_vulkan_prores(codec);
    gtk_widget_set_sensitive(w->profile_combo, profile_sensitive);
    gtk_widget_set_sensitive(w->deblock_combo, codec_uses_software_prores(codec));

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
            codec_uses_any_vulkan(codec) &&
            (w->linux_codec_support.has_prores_ks_vulkan ||
             w->linux_codec_support.has_h264_vulkan ||
             w->linux_codec_support.has_hevc_vulkan ||
             w->linux_codec_support.has_av1_vulkan);
        gtk_widget_set_visible(w->vulkan_device_label, show_vulkan_device);
        gtk_widget_set_visible(w->vulkan_device_combo, show_vulkan_device);
    }

    {
        gboolean show_vaapi_device = codec_uses_linux_vaapi(codec);
        gtk_widget_set_visible(w->vaapi_device_label, show_vaapi_device);
        gtk_widget_set_visible(w->vaapi_device_combo, show_vaapi_device);
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
static void on_codec_changed(GObject *obj, GParamSpec *pspec, AppWidgets *w)
{
    (void)obj;
    (void)pspec;
    /* Repopulate here (rather than in update_dependent_widgets) so that
     * unrelated dependent-widget refreshes (e.g. audio_norm changes) don't
     * reset a manually-picked Vulkan device back to "auto" every time. */
    populate_vulkan_device_combo(w);
    schedule_update_dependent_widgets(w);
}

/* ------------------------------------------------------------------ */
/* Callback: audio_norm combo changed                                  */
/* ------------------------------------------------------------------ */
static void on_audio_norm_changed(GObject *obj, GParamSpec *pspec, AppWidgets *w)
{
    (void)obj;
    (void)pspec;
    schedule_update_dependent_widgets(w);
}

/* ------------------------------------------------------------------ */
/* File list helper                                                    */
/* ------------------------------------------------------------------ */

/* Add a single file path to the listbox and backing array.
 * Silently skips duplicates. */
static void add_file_to_list(AppWidgets *w, const char *path)
{
    guint i;

    if (!path || path[0] == '\0')
        return;

    /* Deduplication check */
    for (i = 0; i < w->file_paths->len; i++) {
        if (g_str_equal(g_ptr_array_index(w->file_paths, i), path))
            return;
    }

    char *stored = g_strdup(path);
    GtkWidget *label = gtk_label_new(stored);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_list_box_append(GTK_LIST_BOX(w->file_listbox), label);
    g_object_set_data(G_OBJECT(label), "file_path", stored);
    g_ptr_array_add(w->file_paths, stored);
}

/* ------------------------------------------------------------------ */
/* Add files button — GtkFileDialog async                             */
/* ------------------------------------------------------------------ */
static void on_add_files_finish(GObject *source, GAsyncResult *res, gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;
    GListModel *files = gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(source), res, NULL);

    if (!files)
        return;

    guint n = g_list_model_get_n_items(files);
    for (guint i = 0; i < n; i++) {
        GFile *file = g_list_model_get_item(files, i);
        char *path = g_file_get_path(file);
        add_file_to_list(w, path);
        g_free(path);
        g_object_unref(file);
    }
    g_object_unref(files);
    schedule_update_dependent_widgets(w);
}

static void on_add_files_clicked(GtkButton *button, AppWidgets *w)
{
    (void)button;
    GtkFileDialog *fd = gtk_file_dialog_new();
    gtk_file_dialog_set_title(fd, "Select Files");

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Video files");
    gtk_file_filter_add_mime_type(filter, "video/*");
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(fd, G_LIST_MODEL(filters));
    g_object_unref(filter);
    g_object_unref(filters);

    gtk_file_dialog_open_multiple(fd, GTK_WINDOW(w->window), NULL,
                                  on_add_files_finish, w);
    g_object_unref(fd);
}

/* ------------------------------------------------------------------ */
/* Add video track button — GtkFileDialog async                       */
/* ------------------------------------------------------------------ */
static void on_add_track_finish(GObject *source, GAsyncResult *res, gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, NULL);

    if (!file)
        return;

    char *path = g_file_get_path(file);
    if (path)
        set_video_track(w, path);
    g_free(path);
    g_object_unref(file);
}

static void on_add_track_clicked(GtkButton *button, AppWidgets *w)
{
    (void)button;
    GtkFileDialog *fd = gtk_file_dialog_new();
    gtk_file_dialog_set_title(fd, "Select Video Track");

    if (w->video_track_path && w->video_track_path[0] != '\0') {
        GFile *initial = g_file_new_for_path(w->video_track_path);
        gtk_file_dialog_set_initial_file(fd, initial);
        g_object_unref(initial);
    }

    gtk_file_dialog_open(fd, GTK_WINDOW(w->window), NULL,
                         on_add_track_finish, w);
    g_object_unref(fd);
}

/* ------------------------------------------------------------------ */
/* Output directory button — GtkFileDialog async                      */
/* ------------------------------------------------------------------ */
static void on_output_dir_finish(GObject *source, GAsyncResult *res, gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;
    GFile *file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), res, NULL);

    if (!file)
        return;

    char *path = g_file_get_path(file);
    if (path)
        set_output_dir(w, path);
    g_free(path);
    g_object_unref(file);
}

static void on_output_dir_clicked(GtkButton *button, AppWidgets *w)
{
    (void)button;
    GtkFileDialog *fd = gtk_file_dialog_new();
    gtk_file_dialog_set_title(fd, "Select Output Directory");

    if (w->output_dir_path && w->output_dir_path[0] != '\0') {
        GFile *initial = g_file_new_for_path(w->output_dir_path);
        gtk_file_dialog_set_initial_folder(fd, initial);
        g_object_unref(initial);
    }

    gtk_file_dialog_select_folder(fd, GTK_WINDOW(w->window), NULL,
                                  on_output_dir_finish, w);
    g_object_unref(fd);
}

typedef struct {
    AppWidgets *w;
    M4VOptions opts;
    GtkWidget *video_spin;
    GtkWidget *audio_spin;
    GtkWidget *ac3_spin;
    GtkWidget *chapters_check;
    GtkWidget *edit_check;
    GtkWidget *lang_entry;
} M4VDialogData;

/* Called when the "Start" button is clicked in the M4V options window. */
static void on_m4v_start_clicked(GtkButton *btn, GtkWidget *win)
{
    (void)btn;
    M4VDialogData *data = g_object_get_data(G_OBJECT(win), "m4v_data");
    if (!data)
        return;

    const char *lang;
    data->opts.video_track_index = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(data->video_spin));
    data->opts.audio_track_index = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(data->audio_spin));
    data->opts.ac3_bitrate_kbps  = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(data->ac3_spin));
    data->opts.add_chapters      = gtk_check_button_get_active(GTK_CHECK_BUTTON(data->chapters_check));
    data->opts.edit_before_mux   = gtk_check_button_get_active(GTK_CHECK_BUTTON(data->edit_check)) ? 1 : 0;

    lang = gtk_editable_get_text(GTK_EDITABLE(data->lang_entry));
    if (!lang || lang[0] == '\0')
        lang = "rus";
    g_strlcpy(data->opts.audio_lang, lang, sizeof(data->opts.audio_lang));

    data->w->pending_m4v_options = data->opts;
    start_m4v_creation(data->w);
    gtk_window_destroy(GTK_WINDOW(win));
}

/* Called when the "Cancel" button or window close button is clicked. */
static void on_m4v_cancel_clicked(GtkButton *btn, GtkWidget *win)
{
    (void)btn;
    gtk_window_destroy(GTK_WINDOW(win));
}

static void prompt_m4v_options_async(AppWidgets *w)
{
    GtkWidget *win;
    GtkWidget *header;
    GtkWidget *cancel_btn;
    GtkWidget *start_btn;
    GtkWidget *grid;
    M4VDialogData *data;

    /* Heap-allocated context; freed automatically via g_object_set_data_full
     * when the window is destroyed. */
    data = g_new0(M4VDialogData, 1);
    data->w    = w;
    data->opts = w->pending_m4v_options;

    win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "Apple m4v creator options");
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(w->window));
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(win), 320, -1);

    /* Attach data to window; freed with g_free when window is finalized. */
    g_object_set_data_full(G_OBJECT(win), "m4v_data", data, g_free);

    /* Header bar with Cancel (leading) and Start (trailing, suggested) */
    header = gtk_header_bar_new();
    cancel_btn = gtk_button_new_with_label("Cancel");
    start_btn  = gtk_button_new_with_label("Start");
    gtk_widget_add_css_class(start_btn, "suggested-action");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), cancel_btn);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), start_btn);
    gtk_window_set_titlebar(GTK_WINDOW(win), header);

    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_m4v_cancel_clicked), win);
    g_signal_connect(start_btn,  "clicked", G_CALLBACK(on_m4v_start_clicked),  win);

    /* Content grid */
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);

    data->video_spin = gtk_spin_button_new_with_range(0, 16, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->video_spin), data->opts.video_track_index);
    data->audio_spin = gtk_spin_button_new_with_range(0, 16, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->audio_spin), data->opts.audio_track_index);
    data->ac3_spin = gtk_spin_button_new_with_range(96, 1536, 32);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->ac3_spin), data->opts.ac3_bitrate_kbps);
    data->chapters_check = gtk_check_button_new_with_label("Add chapters");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(data->chapters_check), data->opts.add_chapters != 0);
    data->edit_check = gtk_check_button_new_with_label(
        "Edit main files before mux (main worker \xe2\x86\x92 m4v \xe2\x86\x92 cleanup)");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(data->edit_check), data->opts.edit_before_mux != 0);
    data->lang_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(data->lang_entry),
                          data->opts.audio_lang[0] != '\0' ? data->opts.audio_lang : "rus");

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Video track index:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), data->video_spin,    1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Audio track index:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), data->audio_spin,    1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("AC3 bitrate kbps:"),  0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), data->ac3_spin,      1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Audio language:"),    0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), data->lang_entry,    1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), data->chapters_check, 0, 4, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), data->edit_check,     0, 5, 2, 1);

    gtk_window_set_child(GTK_WINDOW(win), grid);
    gtk_window_present(GTK_WINDOW(win));
    /* Returns immediately; button callbacks handle accept/cancel. */
}

static void on_apple_m4v_clicked(GtkButton *button, AppWidgets *w)
{
    (void)button;
    prompt_m4v_options_async(w);
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
        path = (char *)g_object_steal_data(G_OBJECT(child), "file_path");

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

    /* ----- profile (preset) ----- */
    if (gtk_widget_get_sensitive(w->profile_combo)) {
        char *preset = get_dropdown_text(w->profile_combo);
        g_strlcpy(opts->preset, preset ? preset : "default", sizeof(opts->preset));
        g_free(preset);
    } else {
        g_strlcpy(opts->preset, "default", sizeof(opts->preset));
    }

    /* ----- deblock ----- */
    if (gtk_widget_get_sensitive(w->deblock_combo)) {
        guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->deblock_combo));
        opts->deblock = (sel != GTK_INVALID_LIST_POSITION) ? (int)sel + 1 : 0;
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
        guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->genre_combo));
        opts->genre = (sel != GTK_INVALID_LIST_POSITION) ? (int)sel + 1 : 0;
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

    if (codec_uses_linux_vaapi(opts->codec)) {
        /* "" (auto) → converter_set_options() fills the first working node. */
        get_selected_vaapi_device(w, opts->hw_device, sizeof(opts->hw_device));
    }

    if (codec_uses_any_vulkan(opts->codec)) {
        int selected_device = get_selected_vulkan_device_index(w);
        if (selected_device >= 0) {
            opts->vulkan_device = selected_device;
        } else {
            int fallback_index, fallback_mask;
            (void)fallback_mask;
            get_vulkan_probe_for_codec(w, opts->codec, &fallback_index, &fallback_mask);
            opts->vulkan_device = (fallback_index >= 0) ? fallback_index : 1;
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
    GtkStringObject *obj =
        GTK_STRING_OBJECT(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(dropdown)));
    if (!obj)
        return g_strdup("");
    return g_strdup(gtk_string_object_get_string(obj));
}

/* ------------------------------------------------------------------ */
/* Keyboard shortcuts                                                  */
/* ------------------------------------------------------------------ */

static void on_add_files_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    AppWidgets *w = (AppWidgets *)user_data;
    on_add_files_clicked(NULL, w);
}

static void on_remove_file_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    AppWidgets *w = (AppWidgets *)user_data;
    on_remove_file_clicked(NULL, w);
}

static void on_clear_list_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    AppWidgets *w = (AppWidgets *)user_data;
    on_clear_list_clicked(NULL, w);
}

static void on_start_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    AppWidgets *w = (AppWidgets *)user_data;
    start_conversion(w);
}

static void on_stop_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    AppWidgets *w = (AppWidgets *)user_data;
    stop_conversion(w);
}

void setup_keyboard_shortcuts(GtkApplication *app, AppWidgets *w)
{
    static const struct {
        const char *name;
        GCallback   handler;
        const char *accel;
    } actions[] = {
        { "add-files",    G_CALLBACK(on_add_files_action),   "<Ctrl>o"      },
        { "remove-file",  G_CALLBACK(on_remove_file_action), "Delete"       },
        { "clear-list",   G_CALLBACK(on_clear_list_action),  "<Ctrl>l"      },
        { "start",        G_CALLBACK(on_start_action),       "<Ctrl>Return" },
        { "stop",         G_CALLBACK(on_stop_action),        "Escape"       },
    };

    for (gsize i = 0; i < G_N_ELEMENTS(actions); i++) {
        GSimpleAction *action = g_simple_action_new(actions[i].name, NULL);
        g_signal_connect(action, "activate", actions[i].handler, w);
        g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
        g_object_unref(action);

        char detailed[64];
        g_snprintf(detailed, sizeof(detailed), "app.%s", actions[i].name);
        const char *accels[] = { actions[i].accel, NULL };
        gtk_application_set_accels_for_action(app, detailed, accels);
    }
}

