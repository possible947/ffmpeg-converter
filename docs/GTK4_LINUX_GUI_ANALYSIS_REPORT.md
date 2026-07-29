# GTK4 Linux GUI Analysis Report
## ffmpeg-converter Video Converter — C Implementation

**Date:** 2025-01-XX  
**Scope:** Linux GTK4 GUI implementation (`src/gui/`)  
**Version:** 2.5

---

## Executive Summary

The Linux GTK4 GUI implementation has several critical issues affecting stability, usability, and modern desktop integration. The three main problems reported by the user (graphics freeze, no theme support, no drag-and-drop) are confirmed, along with additional architectural and UX issues identified during code review.

---

## Table of Contents

1. [Critical Issues](#1-critical-issues)
   - 1.1 Graphics Subsystem Freeze on Launch
   - 1.2 No Light/Dark Theme Support
   - 1.3 No Drag-and-Drop Support
2. [Architecture & Design Issues](#2-architecture--design-issues)
3. [Memory Management Issues](#3-memory-management-issues)
4. [User Experience Issues](#4-user-experience-issues)
5. [Code Quality Issues](#5-code-quality-issues)
6. [Recommendations & Fixes](#6-recommendations--fixes)

---

## 1. Critical Issues

### 1.1 Graphics Subsystem Freeze on Launch

**Severity:** CRITICAL  
**Location:** `gui_main.c:54-56`, `platform/linux/runtime_probe.c`

**Problem:**
The `linux_probe_codec_support()` function is called synchronously on the main GTK thread during application activation, **before the window is presented**:

```c
// gui_main.c:54-56
#if defined(__linux__)
    linux_probe_codec_support(&w->linux_codec_support);
#endif

/* Create the main window and all widgets */
w->window = create_main_window(app, w);
```

The runtime probe performs:
- Multiple `ffmpeg` and `ffprobe` process spawns
- GPU device enumeration (Vulkan, VAAPI)
- File system checks for bundled binaries
- Codec capability testing

**Impact:**
- Application appears frozen for 2-10+ seconds depending on system
- GTK main loop is blocked, preventing any UI rendering
- On slow systems or with many GPU devices, this can exceed 30 seconds
- Users may force-kill the application thinking it crashed

**Root Cause:**
Heavy I/O and process spawning operations executed on the main thread instead of being deferred or run asynchronously.

---

### 1.2 No Light/Dark Theme Support

**Severity:** HIGH  
**Location:** `gui_window.c` (entire file), `gui_main.c`

**Problem:**
The application does not respect the system's light/dark theme preference. All widgets use default GTK4 styling without any custom CSS or adaptive theming.

**Evidence:**
1. No `GtkCssProvider` is created or applied
2. No `gtk_style_context_add_provider_for_display()` calls
3. No theme detection via `g_settings_bind()` or `gtk_settings_get_for_screen()`
4. Hardcoded widget colors/styles (none exist, but default GTK may not match user preferences)
5. No `gtk4.css` or theme-aware styling

**Impact:**
- Poor integration with desktop environments (GNOME, KDE, etc.)
- Inconsistent appearance with system theme
- Accessibility issues for users relying on high-contrast themes
- No way for users to customize appearance

---

### 1.3 No Drag-and-Drop Support

**Severity:** MEDIUM  
**Location:** `gui_window.c`

**Problem:**
The file list widget (`GtkListBox`) does not accept dropped files. Users must always click "Add files..." and navigate through a file picker dialog.

**Evidence:**
1. No `gtk_drag_dest_set()` calls on `w->file_listbox`
2. No "drag-data-received" signal handlers
3. No "drag-drop" signal handlers
4. No `GdkDragAction` configuration

**Impact:**
- Inefficient workflow for power users
- Inconsistent with modern desktop application expectations
- Cannot integrate with file managers (Nautilus, Dolphin, etc.)

---

## 2. Architecture & Design Issues

### 2.1 Global State Variable (`g_widgets`)

**Severity:** HIGH  
**Location:** `gui_callbacks.c:45`

```c
static AppWidgets *g_widgets = NULL;
```

**Problem:**
A global pointer to `AppWidgets` is used to pass widget state to callback functions. This is set in `run_converter()` and cleared in `shutdown_conversion()`.

**Issues:**
- Breaks encapsulation and makes testing difficult
- Potential race condition if callbacks fire during shutdown
- Memory safety issue: if `g_widgets` is accessed after `AppWidgets` is freed
- Makes the code non-reentrant (only one instance can exist)

**Recommendation:** Use proper callback context passing via `g_idle_add_full()` user data.

---

### 2.2 Synchronous Window Creation with Heavy Initialization

**Severity:** MEDIUM  
**Location:** `gui_main.c:58-60`, `gui_window.c:150-300`

**Problem:**
The entire UI is built synchronously in `create_main_window()`, including populating codec combinations from runtime probe results. If probe data is incomplete or invalid, the window creation may fail or produce incorrect UI state.

**Recommendation:**
- Show a loading/splash screen immediately
- Build basic UI structure first
- Populate dynamic content asynchronously after probe completes

---

### 2.3 Modal Dialog Pattern for M4V Options

**Severity:** MEDIUM  
**Location:** `gui_window.c:560-620` (`prompt_m4v_options`)

**Problem:**
The Apple M4V options dialog creates a nested `GMainLoop`:

```c
loop = g_main_loop_new(NULL, FALSE);
// ...
g_main_loop_run(loop);
```

**Issues:**
- Nested main loops are discouraged in GTK/GIO
- Can cause event processing issues
- Blocks the entire application during dialog display
- Risk of deadlock if signals are not properly routed

**Recommendation:** Use async callbacks or `GtkDialog` response signals instead.

---

### 2.4 Unused Glade File

**Severity:** LOW  
**Location:** `src/ffmpeg_convert.glade`

**Problem:**
A Glade UI definition file exists but is not used. The entire UI is built programmatically in `gui_window.c`.

**Impact:**
- Maintenance overhead (two UI definitions)
- Potential confusion for contributors
- The Glade file may be outdated

**Recommendation:** Either use the Glade file or remove it.

---

## 3. Memory Management Issues

### 3.1 File Path Storage in `GPtrArray`

**Severity:** MEDIUM  
**Location:** `gui_window.c:430-440`, `gui_window.c:680-690`

**Problem:**
When files are added to the list:

```c
char *path_copy = g_strdup(path);
// ...
g_ptr_array_add(w->file_paths, path_copy);
```

But when removing:

```c
g_ptr_array_remove(w->file_paths, path);
```

**Issues:**
- `g_ptr_array_remove()` removes by value, not by index
- The `path` pointer may not match exactly due to strdup
- Potential memory leak if removal fails
- No NULL termination of the array

**Recommendation:** Use `g_ptr_array_remove_index()` or maintain proper array semantics.

---

### 3.2 Missing Cleanup of `output_dir_path` and `video_track_path`

**Severity:** MEDIUM  
**Location:** `gui_window.h:35-36`

**Problem:**
The `AppWidgets` struct contains dynamically allocated strings:

```c
char *output_dir_path;
char *video_track_path;
```

But in `shutdown_conversion()` and `on_app_shutdown()`, these are never freed.

**Impact:**
- Memory leak on every application run
- Accumulates over time if application is restarted within same session

**Recommendation:** Add cleanup in shutdown handlers.

---

### 3.3 `GPtrArray` Not Freed

**Severity:** MEDIUM  
**Location:** `gui_window.h:20`

**Problem:**
`w->file_paths` is a `GPtrArray` allocated with `g_ptr_array_new_with_free_func()`, but it is never freed when the application shuts down.

**Recommendation:** Free in shutdown handler.

---

## 4. User Experience Issues

### 4.1 No Application Icon

**Severity:** MEDIUM  
**Location:** `gui_main.c`, `gui_window.c`

**Problem:**
- No `gtk_window_set_icon()` or `gtk_window_set_icon_name()` calls
- Window appears with generic icon in taskbar/window list
- `icon.png` exists in `src/gui/` but is not used

**Recommendation:** Set application icon from bundled resource.

---

### 4.2 No Keyboard Shortcuts

**Severity:** LOW  
**Location:** `gui_window.c`

**Problem:**
No accelerator keys defined for common actions:
- Ctrl+O for "Add files"
- Ctrl+S for "Start"
- Ctrl+W for "Clear list"
- Delete for "Remove selected"

**Recommendation:** Add `GtkAcceleratorGroup` or use `gtk_accelerator_parse()`.

---

### 4.3 Progress Bar Text During Analysis

**Severity:** LOW  
**Location:** `gui_callbacks.c:310-320`

**Problem:**
During analysis mode, the progress bar shows only percentage without ETA:

```c
if (payload->analysis_mode)
    snprintf(txt, sizeof(txt), "%d%%", (int)(percent + 0.5));
```

But the status label shows ETA separately. This is inconsistent.

**Recommendation:** Show consistent information in both places.

---

### 4.4 "Apple m4v..." Button Always Visible on Linux

**Severity:** LOW  
**Location:** `gui_window.c:145-147`

**Problem:**
The Apple M4V creator button is always visible on Linux, but this is a macOS-specific feature. While it does work on Linux (creating M4V files), the naming is confusing.

**Recommendation:** Consider renaming to "M4V Creator..." or adding a tooltip explaining functionality.

---

### 4.5 No Input Validation Feedback

**Severity:** MEDIUM  
**Location:** `gui_window.c:690-710`

**Problem:**
When user clicks "Start" without adding files or setting output directory, the application silently does nothing or produces confusing error messages in the log.

**Recommendation:** Show a `GtkMessageDialog` with clear error messages before starting.

---

### 4.6 Fixed Window Size

**Severity:** LOW  
**Location:** `gui_main.c:68`

**Problem:**
```c
gtk_window_set_default_size(GTK_WINDOW(w->window), 800, 600);
```

While the window is resizable, 800x600 may be too small for modern displays with HiDPI.

**Recommendation:** Consider larger default size or make it configurable.

---

## 5. Code Quality Issues

### 5.1 Deprecated GTK API Usage

**Severity:** LOW  
**Location:** `gui_window.c` (multiple places)

**Problem:**
The CMakeLists.txt suppresses deprecation warnings:
```cmake
target_compile_options(gui_lib PRIVATE -Wno-deprecated-declarations)
```

Some deprecated APIs are used:
- `gtk_file_chooser_dialog_new()` — use `GtkFileChooserNative`
- `gtk_dialog_new_with_buttons()` — use newer dialog patterns
- `gtk_editable_set_text()` / `gtk_editable_get_text()` — use `GtkEditable` interface

**Recommendation:** Update to modern GTK4 APIs.

---

### 5.2 Magic Numbers

**Severity:** LOW  
**Location:** `gui_window.c:155-160`

**Problem:**
Hardcoded values for grid spacing, margins, and widget sizes:
```c
gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
gtk_widget_set_margin_top(grid, 12);
```

**Recommendation:** Use named constants or CSS for styling.

---

### 5.3 No Error Handling for GTK Operations

**Severity:** LOW  
**Location:** Throughout `gui_window.c`

**Problem:**
GTK function return values are generally not checked. If widget creation fails, the application may crash later.

**Recommendation:** Add basic error checking for critical operations.

---

### 5.4 Thread Safety Concerns

**Severity:** MEDIUM  
**Location:** `gui_callbacks.c:400-450`

**Problem:**
The `g_widgets` global is accessed from both the worker thread and main thread without synchronization in some paths.

**Recommendation:** Ensure all UI updates go through `g_idle_add()` and protect shared state with mutexes.

---

## 6. Recommendations & Fixes

### 6.1 Fix Graphics Freeze (Priority: CRITICAL)

**Solution:** Move codec probing to a background thread.

```c
// gui_main.c - Modified activate_cb
static void activate_cb(GtkApplication *app, gpointer user_data)
{
    AppWidgets *w = g_new0(AppWidgets, 1);
    // ... initialization ...
    
    // Create window immediately (show loading state)
    w->window = create_main_window(app, w);
    gtk_window_present(GTK_WINDOW(w->window));
    
    // Show loading indicator
    gtk_label_set_text(w->status_label, "Initializing...");
    
    // Start async probe
    g_thread_new("codec-probe", async_codec_probe, w);
}

static gpointer async_codec_probe(gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;
    
#if defined(__linux__)
    linux_probe_codec_support(&w->linux_codec_support);
#endif
    
    // Update UI on main thread
    g_idle_add_full(G_PRIORITY_DEFAULT, update_codec_ui_after_probe, w, NULL);
    return NULL;
}
```

---

### 6.2 Add Theme Support (Priority: HIGH)

**Solution:** Apply adaptive CSS styling.

```c
// In create_main_window() or a separate init function
static void apply_theme(GtkWindow *window)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    
    // Detect dark mode
    GtkSettings *settings = gtk_settings_get_for_display(gdk_display_get_default());
    gboolean is_dark = FALSE;
    g_object_get(settings, "gtk-application-prefer-dark-theme", &is_dark, NULL);
    
    const char *css;
    if (is_dark) {
        css = "/* Dark theme styles */\n"
              "listbox row { background-color: #2d2d2d; color: #ffffff; }\n"
              "grid { background-color: #1e1e1e; }\n";
    } else {
        css = "/* Light theme styles */\n"
              "listbox row { background-color: #ffffff; color: #000000; }\n"
              "grid { background-color: #f5f5f5; }\n";
    }
    
    gtk_css_provider_load_from_data(provider, css, -1);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
    
    // Listen for theme changes
    g_signal_connect(settings, "notify::gtk-application-prefer-dark-theme",
                     G_CALLBACK(on_theme_changed), window);
}
```

---

### 6.3 Add Drag-and-Drop (Priority: MEDIUM)

**Solution:** Enable DnD on the file list.

```c
// In create_main_window(), after creating w->file_listbox:
static void setup_drag_drop(GtkWidget *listbox, AppWidgets *w)
{
    GtkTargetList *targets = gtk_target_list_new(NULL, 0);
    gtk_target_list_add_uri_targets(targets, 0);
    
    gtk_drag_dest_set(listbox, GTK_DEST_DEFAULT_ALL,
                      targets, 1, GDK_ACTION_COPY);
    
    g_signal_connect(listbox, "drag-data-received",
                     G_CALLBACK(on_drag_data_received), w);
    
    gtk_target_list_unref(targets);
}

static void on_drag_data_received(GtkWidget *widget,
                                   GdkDragContext *context,
                                   gint x, gint y,
                                   GtkSelectionData *data,
                                   guint info, guint time_,
                                   AppWidgets *w)
{
    gchar **uris = gtk_selection_data_get_uris(data);
    if (!uris) return;
    
    for (gchar **uri = uris; *uri; uri++) {
        char *path = g_filename_from_uri(*uri, NULL, NULL);
        if (path) {
            add_file_to_list(w, path);
            g_free(path);
        }
    }
    g_strfreev(uris);
    
    gtk_drag_finish(context, TRUE, FALSE, time_);
}
```

---

### 6.4 Fix Memory Management (Priority: HIGH)

**Solution:** Proper cleanup in shutdown.

```c
void shutdown_conversion(AppWidgets *w)
{
    if (!w) return;
    
    g_mutex_lock(&w->thread_lock);
    w->shutting_down = TRUE;
    g_widgets = NULL;
    w->stop_requested = 1;
    
    if (w->current_converter)
        converter_stop(w->current_converter);
    
    GThread *thread = w->worker_thread;
    w->worker_thread = NULL;
    g_mutex_unlock(&w->thread_lock);
    
    if (thread)
        g_thread_join(thread);
    
    // Free dynamic strings
    g_free(w->output_dir_path);
    g_free(w->video_track_path);
    
    // Free file paths array
    g_ptr_array_unref(w->file_paths);
}
```

Also modify `on_app_shutdown()`:
```c
static void on_app_shutdown(GApplication *app, gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)g_object_get_data(G_OBJECT(app), "app_widgets");
    if (w) {
        shutdown_conversion(w);
        g_mutex_clear(&w->thread_lock);
        g_free(w);
    }
}
```

---

### 6.5 Replace Global `g_widgets` (Priority: HIGH)

**Solution:** Pass widget pointer through callback context.

```c
// In callback structs, add AppWidgets pointer
typedef struct {
    AppWidgets *w;  // Already exists in most structs
    // ... other fields
} LogUpdateData;

// Remove global:
// static AppWidgets *g_widgets = NULL;

// In run_converter(), set g_widgets locally:
g_widgets = w;  // Keep this but document it's only for callbacks

// Better: Pass w through all callback data structs (already done)
// Then remove g_widgets entirely and use data->w in all callbacks
```

---

### 6.6 Add Input Validation (Priority: MEDIUM)

**Solution:** Show error dialogs before starting.

```c
static gboolean validate_conversion_input(AppWidgets *w)
{
    if (w->file_paths->len == 0) {
        show_error_dialog(w->window, "No files selected",
                         "Please add at least one file to convert.");
        return FALSE;
    }
    
    char *codec = get_dropdown_text(w->codec_combo);
    if (codec_is_mux(codec)) {
        if (!w->video_track_path || w->video_track_path[0] == '\0') {
            show_error_dialog(w->window, "Video track not set",
                             "Mux mode requires a video track file.");
            g_free(codec);
            return FALSE;
        }
    }
    g_free(codec);
    
    return TRUE;
}

static void on_start_clicked(GtkButton *button, AppWidgets *w)
{
    (void)button;
    if (!validate_conversion_input(w))
        return;
    start_conversion(w);
}
```

---

### 6.7 Set Application Icon (Priority: LOW)

**Solution:** Load icon from resources.

```c
// In activate_cb, after creating window:
GdkPixbuf *icon = gdk_pixbuf_new_from_file_at_size(
    ICON_PATH, 48, 48, NULL);
if (icon) {
    gtk_window_set_icon(GTK_WINDOW(w->window), icon);
    g_object_unref(icon);
}
gtk_window_set_icon_name(GTK_WINDOW(w->window), "video-x-generic");
```

---

### 6.8 Modernize GTK API Usage (Priority: LOW)

**Solution:** Replace deprecated APIs.

| Deprecated | Modern Replacement |
|------------|-------------------|
| `gtk_file_chooser_dialog_new()` | `gtk_file_chooser_native_new()` |
| `gtk_dialog_new_with_buttons()` | `gtk_message_dialog_new()` or custom dialog |
| `gtk_editable_set_text()` | `gtk_entry_set_text()` |
| `gtk_widget_show()` | Already shown by default in GTK4 |

---

## Summary of Issues by Severity

| Severity | Count | Issues |
|----------|-------|--------|
| CRITICAL | 1 | Graphics freeze on launch |
| HIGH | 4 | No theme support, global state, memory leaks (x2) |
| MEDIUM | 7 | No DnD, nested main loop, file array issues, no icon, no validation, thread safety, modal dialog |
| LOW | 8 | Unused Glade, no shortcuts, progress bar, button naming, magic numbers, error handling, deprecated APIs, window size |

---

## Recommended Fix Order

1. **Immediate (Critical):** Fix graphics freeze by moving codec probe to background thread
2. **Short-term (High):** Fix memory leaks and remove global state
3. **Medium-term (High/Medium):** Add theme support and drag-and-drop
4. **Long-term (Low):** Modernize APIs, add keyboard shortcuts, improve UX

---

## Appendix: File Reference

| File | Purpose | Issues Found |
|------|---------|--------------|
| `gui_main.c` | Application entry point | Synchronous probe, no icon, no cleanup |
| `gui_window.c` | UI construction | No DnD, no theme, deprecated APIs, magic numbers |
| `gui_window.h` | Data structures | Missing cleanup in shutdown |
| `gui_callbacks.c` | Event handling | Global state, thread safety concerns |
| `ffmpeg_convert.glade` | UI definition | Unused |
