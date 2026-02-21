/*
 *  gui_callbacks.h
 *  Header for the GUI‑callback handling module.
 *
 *  Provides two public functions that are called from the UI
 *  (Start / Stop buttons).  The implementation (see gui_callbacks.c)
 *  creates a separate thread that runs the converter, updates the
 *  Gtk widgets through GIdle callbacks, and forwards the
 *  ConverterCallbacks to the converter instance.
 *
 *  The header is intentionally minimal – it only declares the
 *  two functions needed by the UI and includes the necessary
 *  types (`AppWidgets` and `Converter`).
 */

#ifndef GUI_CALLBACKS_H
#define GUI_CALLBACKS_H

#include "gui_window.h"
#include "converter.h"

/* Start the conversion in a background thread. */
void start_conversion(AppWidgets *w);

/* Request the running converter to stop. */
void stop_conversion(AppWidgets *w);

#endif /* GUI_CALLBACKS_H */
