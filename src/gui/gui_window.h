/*  gui_window.h
 *  Declaration of AppWidgets and helper prototypes.
 */

#ifndef GUI_WINDOW_H
#define GUI_WINDOW_H

#include <gtk/gtk.h>
#include "converter.h"

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
    GtkWidget *genre_combo;

    GtkWidget *overwrite_check;
    GtkWidget *output_dir_label;
    GtkWidget *output_dir_btn;
    char *output_dir_path;

    GtkWidget *add_files_btn;
    GtkWidget *remove_file_btn;
    GtkWidget *clear_list_btn;
    GtkWidget *start_btn;
    GtkWidget *stop_btn;

    GtkWidget *progress_bar;
    GtkWidget *status_label;
    GtkWidget *log_view;            /* GtkTextView */
    GtkTextBuffer *log_buffer;

    /* Status tracking */
    char *last_status;              /* stores the last status message */

    /* Threading */
    GThread *worker_thread;
    GMutex   thread_lock;           /* protects worker_thread */
    Converter *current_converter;   /* protected by thread_lock */
} AppWidgets;

/* Creation */
GtkWidget* create_main_window(GtkApplication *app, AppWidgets *w);

/* Helper to gather options from GUI */
void collect_options_from_gui(AppWidgets *w,
                              ConvertOptions *opts,
                              char ***out_files,
                              int   *out_count);

/* File list helpers */
void clear_file_list(AppWidgets *w);

/* Start / stop conversion */
void start_conversion(AppWidgets *w);
void stop_conversion(AppWidgets *w);

#endif /* GUI_WINDOW_H */
