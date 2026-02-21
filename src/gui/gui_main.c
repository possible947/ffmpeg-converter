/*  gui_main.c
 *  Entry point for the GTK4 GUI application.
 */

#include <gtk/gtk.h>
#include "gui_window.h"
#include "gui_callbacks.h"

/* Forward declaration of the activate handler */
static void activate_cb(GtkApplication *app, gpointer user_data);

/* main --------------------------------------------------------*/
int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

    app = gtk_application_new("com.example.ffmpeg_converter.gui", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate_cb), NULL);

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}

/* activate_cb ---------------------------------------------------*/
static void activate_cb(GtkApplication *app, gpointer user_data)
{
    AppWidgets *w = g_new0(AppWidgets, 1);
    g_mutex_init(&w->thread_lock);
    w->current_converter = NULL;

    /* Create the main window and all widgets */
    w->window = create_main_window(app, w);
    gtk_window_set_title(GTK_WINDOW(w->window), "ffmpeg-converter GUI");
    gtk_window_set_default_size(GTK_WINDOW(w->window), 800, 600);
    gtk_window_set_resizable(GTK_WINDOW(w->window), TRUE);

    /* Present the window */
    gtk_window_present(GTK_WINDOW(w->window));
}
