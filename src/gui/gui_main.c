/*  gui_main.c
 *  Entry point for the GTK4 GUI application.
 */

#include <gtk/gtk.h>
#include "gui_window.h"
#include "gui_callbacks.h"

/* Forward declaration of the activate handler */
static void activate_cb(GtkApplication *app, gpointer user_data);
static gboolean on_window_close_request(GtkWindow *window, gpointer user_data);
static void on_app_shutdown(GApplication *app, gpointer user_data);

/* main --------------------------------------------------------*/
int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

#ifdef __APPLE__
    /* Quartz backend is more stable with the Cairo renderer in this app. */
    g_setenv("GSK_RENDERER", "cairo", FALSE);
#endif

    app = gtk_application_new("com.example.ffmpeg_converter.gui", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate_cb), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_app_shutdown), NULL);

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}

/* activate_cb ---------------------------------------------------*/
static void activate_cb(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    AppWidgets *w = g_new0(AppWidgets, 1);
    g_mutex_init(&w->thread_lock);
    w->current_converter = NULL;
    w->shutting_down = FALSE;
    w->dependent_update_source_id = 0;
    w->pending_job_kind = GUI_JOB_NONE;
    w->active_job_kind = GUI_JOB_NONE;
    w->stop_requested = 0;
    m4v_default_options(&w->pending_m4v_options);

#if defined(__linux__)
    linux_probe_codec_support(&w->linux_codec_support);
#endif

    /* Create the main window and all widgets */
    w->window = create_main_window(app, w);
    g_signal_connect(w->window, "close-request", G_CALLBACK(on_window_close_request), w);
#ifdef __APPLE__
    {
        GtkSettings *settings = gtk_settings_get_default();
        if (settings)
            g_object_set(settings, "gtk-enable-animations", FALSE, NULL);
    }
#endif

    g_object_set_data(G_OBJECT(app), "app_widgets", w);
    gtk_window_set_title(GTK_WINDOW(w->window), "ffmpeg-converter GUI");
    gtk_window_set_default_size(GTK_WINDOW(w->window), 800, 600);
    gtk_window_set_resizable(GTK_WINDOW(w->window), TRUE);

    /* Present the window */
    gtk_window_present(GTK_WINDOW(w->window));
}

static gboolean on_window_close_request(GtkWindow *window, gpointer user_data)
{
    (void)window;
    AppWidgets *w = (AppWidgets *)user_data;
    shutdown_conversion(w);
    return FALSE;
}

static void on_app_shutdown(GApplication *app, gpointer user_data)
{
    (void)user_data;
    AppWidgets *w = (AppWidgets *)g_object_get_data(G_OBJECT(app), "app_widgets");
    if (w)
        shutdown_conversion(w);
}
