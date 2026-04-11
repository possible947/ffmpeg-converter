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

    /* Threading */
    GThread *worker_thread;
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
} AppWidgets;

/* Creation */
GtkWidget* create_main_window(GtkApplication *app, AppWidgets *w);

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
