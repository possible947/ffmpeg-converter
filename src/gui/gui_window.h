/*  gui_window.h
 *  Declaration of AppWidgets and helper prototypes.
 */

#ifndef GUI_WINDOW_H
#define GUI_WINDOW_H

#include <gtk/gtk.h>
#include "converter.h"
#include "m4v.h"
#include "linux/runtime_probe.h"

typedef enum {
    GUI_JOB_NONE = 0,
    GUI_JOB_CONVERT,
    GUI_JOB_M4V
} GuiJobKind;

typedef struct {
    /* Main window */
    GtkWidget *window;

    /* File list */
    GtkWidget *file_listbox;
    GPtrArray *file_paths;          /* GPtrArray of gchar* */

    /* Controls */
    GtkWidget *codec_combo;
    GtkWidget *vulkan_device_label;
    GtkWidget *vulkan_device_combo;
    GtkWidget *vaapi_device_label;
    GtkWidget *vaapi_device_combo;
    GtkWidget *profile_combo;
    GtkWidget *deblock_combo;

    GtkWidget *audio_norm_combo;
    GtkWidget *audio_output_combo;
    GtkWidget *genre_combo;

    GtkWidget *overwrite_check;
    GtkWidget *output_dir_label;
    GtkWidget *output_dir_btn;
    char *output_dir_path;
    GtkWidget *video_track_label;
    GtkWidget *add_track_btn;
    char *video_track_path;

    GtkWidget *add_files_btn;
    GtkWidget *apple_m4v_btn;
    GtkWidget *remove_file_btn;
    GtkWidget *clear_list_btn;
    GtkWidget *start_btn;
    GtkWidget *stop_btn;

    GtkWidget *progress_bar;
    GtkWidget *status_label;
    GtkWidget *log_view;            /* GtkTextView */
    GtkTextBuffer *log_buffer;
    GtkTextMark *log_end_mark;      /* persistent autoscroll mark */

    /* Threading */
    GThread *worker_thread;
    GThread *probe_thread;          /* hardware codec probe thread */
    GMutex   thread_lock;           /* protects worker_thread */
    Converter *current_converter;   /* protected by thread_lock */
    gboolean shutting_down;         /* set during app shutdown */

    /* Deferred UI updates to avoid re-entrancy in signal handlers */
    guint dependent_update_source_id;

    GuiJobKind pending_job_kind;
    GuiJobKind active_job_kind;
    volatile int stop_requested;
    M4VOptions pending_m4v_options;

    /* Linux runtime codec support cache */
    LinuxCodecSupport linux_codec_support;

    /* Backing models for combos that are populated dynamically */
    GtkStringList *codec_list;          /* model for codec_combo */
    GtkStringList *preset_list;         /* model for profile_combo (dynamic presets per codec) */
    GtkStringList *vulkan_device_list;  /* model for vulkan_device_combo */
    GArray        *vulkan_device_ids;   /* parallel: combo-index → vk device number (-1=auto) */
    GtkStringList *vaapi_device_list;   /* model for vaapi_device_combo */
    GArray        *vaapi_device_nodes;  /* parallel: combo-index → render node string ("" = auto) */
} AppWidgets;

/* Creation */
GtkWidget* create_main_window(GtkApplication *app, AppWidgets *w);

/* Register GSimpleActions + accelerators for keyboard shortcuts.
 * Must be called after create_main_window() so that w->window is set. */
void setup_keyboard_shortcuts(GtkApplication *app, AppWidgets *w);

/* Launch hardware codec detection in a background thread.
 * On completion the codec combo is updated on the main thread. */
void start_hw_probe(AppWidgets *w);

/* Toggle editable controls while a job is running. */
void set_running_ui_state(AppWidgets *w, gboolean running);

/* Helper to gather options from GUI */
void collect_options_from_gui(AppWidgets *w,
                              ConvertOptions *opts,
                              char ***out_files,
                              int   *out_count);

/* File list helpers */
void clear_file_list(AppWidgets *w);

/* Start / stop conversion */
void start_conversion(AppWidgets *w);
void start_m4v_creation(AppWidgets *w);
void stop_conversion(AppWidgets *w);

#endif /* GUI_WINDOW_H */
