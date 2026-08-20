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
static void on_theme_changed(GtkSettings *settings, GParamSpec *pspec, gpointer user_data);

/* main --------------------------------------------------------*/
int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

#ifdef __APPLE__
    /* Quartz backend is more stable with the Cairo renderer in this app. */
    g_setenv("GSK_RENDERER", "cairo", FALSE);
#endif

#if defined(__linux__)
    /* Fall back to the Cairo renderer on Linux when no explicit renderer is
     * requested.  The NGL/GL/Vulkan GSK renderers freeze on some Mesa and
     * Nvidia driver combinations; Cairo is always available and stable.
     * Users who want GPU acceleration can override with GSK_RENDERER=ngl. */
    if (!g_getenv("GSK_RENDERER"))
        g_setenv("GSK_RENDERER", "cairo", FALSE);
#endif

    /* Shown in GNOME dock, the applications menu, and window switcher.
     * Must stay in sync with Name= in the AppImage .desktop file. */
    g_set_application_name("FFMpeg-Converter");

    app = gtk_application_new("io.github.possible947.ffmpeg_converter",
                              G_APPLICATION_DEFAULT_FLAGS);
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

    /* Subscribe to system light/dark theme changes so the app can adapt
     * any custom styling when the user switches themes at runtime. */
    {
        GtkSettings *settings = gtk_settings_get_default();
        if (settings)
            g_signal_connect(settings, "notify::gtk-application-prefer-dark-theme",
                             G_CALLBACK(on_theme_changed), w);
    }

    /* Register the embedded icon so GTK's theme resolver can find
     * "ffmpeg-converter" via the hicolor tree in the GResource bundle. */
    gtk_icon_theme_add_resource_path(
        gtk_icon_theme_get_for_display(gdk_display_get_default()),
        "/io/github/possible947/ffmpeg_converter/icons");

    /* Keyboard shortcuts (GSimpleActions on the application). */
    setup_keyboard_shortcuts(app, w);

    g_object_set_data(G_OBJECT(app), "app_widgets", w);
    gtk_window_set_title(GTK_WINDOW(w->window), "FFMpeg-Converter");
    gtk_window_set_default_size(GTK_WINDOW(w->window), 800, 600);
    gtk_window_set_resizable(GTK_WINDOW(w->window), TRUE);

    /* Present the window and set the application icon. */
    gtk_window_present(GTK_WINDOW(w->window));
    gtk_window_set_icon_name(GTK_WINDOW(w->window), "ffmpeg-converter");

#if defined(__linux__)
    /* Detect hardware encoders in a background thread so the window
     * opens immediately.  The codec combo is updated when probe finishes. */
    start_hw_probe(w);
#endif
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

/* on_theme_changed -------------------------------------------*/
/* Called when the system light/dark preference changes at runtime.
 * The drag-hover and log CSS use rgba() values that work in both
 * themes, so no CSS reload is needed.  This hook is the right place
 * to add any future theme-specific adjustments. */
static void on_theme_changed(GtkSettings *settings, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    (void)user_data;

    gboolean dark = FALSE;
    g_object_get(settings, "gtk-application-prefer-dark-theme", &dark, NULL);
    /* Currently all custom styling is theme-neutral (rgba values).
     * This callback is retained as the extension point for future
     * theme-specific tweaks. */
    (void)dark;
}
