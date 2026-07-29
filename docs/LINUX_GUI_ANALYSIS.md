# Linux GTK4 GUI — Analysis Report

**Scope:** C/GTK4 GUI under `src/gui/` (`gui_main.c`, `gui_window.c`, `gui_callbacks.c`,
`gui_window.h`, `gui_callbacks.h`, `CMakeLists.txt`) plus the startup hardware probe in
`src/platform/linux/runtime_probe.c`.

**Date:** 2026-07-29

Findings are grouped by severity. Each item includes the root cause, the exact code
location (`file:line`), the impact, and a concrete fix.

---

## Critical

### C1. Startup freeze — synchronous GPU probing on the main thread

**Symptom:** "The program often freezes the graphics subsystem after launch."

**Root cause:** `activate_cb` calls `linux_probe_codec_support()` **synchronously on the
GTK main thread, before the window is shown** (`src/gui/gui_main.c:51`).

That function (`src/platform/linux/runtime_probe.c:380`) spawns ffmpeg as a child
process via `system()` to test every GPU encoder. In the worst case it launches
**~18 ffmpeg subprocesses** in sequence, each performing a real hardware encode:

| Probe                       | Count          | Code location                  |
|-----------------------------|----------------|--------------------------------|
| VAAPI per `/dev/dri/renderD*` | 2 × (#render nodes) | `runtime_probe.c:430-431`    |
| NVENC (h264/hevc)           | 2              | `runtime_probe.c:448-449`      |
| AMF (h264/hevc)             | 2              | `runtime_probe.c:452-453`      |
| QSV (h264/hevc)             | 2              | `runtime_probe.c:456-457`      |
| Vulkan `vk:0`…`vk:7`        | up to 8        | `runtime_probe.c:308-333`      |

Each `system()` call links ffmpeg dynamically, loads GPU drivers, initializes the
device, and encodes one 1920×1080 frame. Even "fast" failures cost 200–500 ms each
due to driver initialization. With a discrete + integrated GPU (common on laptops) the
total easily reaches **5–15 seconds of a fully frozen main thread**, during which the
window never appears and the compositor is starved of redraws — exactly the reported
"graphics subsystem freeze."

The result is cached (`g_cache`, `runtime_probe.c:386`), so the cost is paid **once per
process** — but that once is the entire startup.

**Fix (recommended):** Move the probe to a background thread and show the window
immediately:

1. In `activate_cb`, build and present the window first, with codec combos
   populated only with the always-available entries (`copy`, `prores`, `prores_ks`,
   `mux`) and a transient "Detecting hardware…" status.
2. Launch `g_thread_new("hw-probe", ...)` to run `linux_probe_codec_support()`.
3. When the probe completes, push the extra codec entries into the combos via
   `g_idle_add` (GTK widget mutation must happen on the main thread), then clear the
   status line.

Because the result is cached, `linux_get_preferred_*` calls later in the run remain
instant.

**Lower-effort mitigation:** If threading the probe is too large a change, at minimum
call `gtk_window_present()` **before** `linux_probe_codec_support()` so the window is
visible (if frozen) during probing, and set the status label to "Detecting hardware…"
so the user knows the app hasn't crashed. This does not fix the freeze, only the
perception of it.

---

### C2. GTK threading violation — widget reads from the worker thread

**Root cause:** `collect_options_from_gui()` reads GTK widget state
(`gtk_combo_box_get_active`, `gtk_combo_box_text_get_active_text`,
`gtk_check_button_get_active`, …) but it is called from **the worker thread**, not the
main thread:

- `src/gui/gui_callbacks.c:460` — called inside `run_converter()`, which is the
  `g_thread_new("converter", …)` body (`gui_callbacks.c:641`).

GTK4 is **not thread-safe**; every widget accessor must run on the main (default)
thread context. Reading widgets from another thread is undefined behavior — it can
deadlock the display server, corrupt internal structures, or contribute to the freeze
the user observes.

**Fix:** Collect the options snapshot **on the main thread before spawning the thread**.
In `start_conversion()` / `start_m4v_creation()` (both run on the main thread via
button signals), call `collect_options_from_gui(w, &opts, &out_files, &out_count)`,
then pass the already-collected `opts` / `file_list` into the thread as a heap struct.
The thread then owns and frees them; it never touches widgets directly. (Progress/log
updates already correctly use `g_idle_add`, so they are fine.)

---

### C3. No light/dark system theme support

**Root cause:** The application never installs a `GtkCssProvider`, never sets
`gtk-application-prefer-dark-theme`, and does not use libadwaita. A search for
`GtkCssProvider`, `color-scheme`, `prefer-dark`, `adw_` across `src/gui/` returned
zero matches.

Raw GTK4 *can* follow the desktop color scheme, but only if the application opts in
and provides at least minimal CSS. As written, the app renders with whatever the
default theme is and has no per-widget styling, so it will look broken/unstyled on many
desktops (notably GNOME with `color-scheme=prefer-dark`).

**Fix (two parts):**

1. **Honor the system color scheme.** In `activate_cb`, read and follow the portal /
   GSetting. The portable GTK4 way is to set the `color-scheme` via
   `GtkSettings` is not directly available, but libadwaita's `AdwStyleManager`
   does this automatically. Recommended path: depend on `libadwaita-1` (it is the
   GTK4-native styling library, available on all Linux distros) and use
   `AdwApplication` / `AdwApplicationWindow`. This gives automatic light/dark
   following for free.

2. **Add application CSS.** Load a `GtkCssProvider` with a small CSS block for the
   custom surfaces (log view monospace, progress bar, file listbox rows), e.g.:
   ```c
   GtkCssProvider *css = gtk_css_provider_new();
   gtk_css_provider_load_from_resource(css, "/com/example/ffmpeg_converter/style.css");
   gtk_style_context_add_provider_for_display(
       gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   ```
   This also lets you fix the unstyled look of the deprecated widgets (see M1).

---

### C4. No drag-and-drop support

**Root cause:** There is no `GtkDropTarget` / `GtkDropTargetAsync` anywhere in the
codebase (confirmed by search). Files can only be added through the "Add files…"
button → native file chooser. This is tedious for a drag-and-drop-oriented media tool.

Notably, the **Pascal/Lazarus GUI already has drag-and-drop** (per
`fpc/CHANGELOG.md:196`: "Drag-and-drop file loading in Lazarus GUI"), so the C GUI is a
feature-parity regression — the two implementations are supposed to be feature-matched
(see `AGENTS.md` / `README.md`).

**Fix:** Attach a `GtkDropTarget` to the file listbox (or the whole window) accepting
`G_FILE_URI_LIST` and `text/uri-list`:

```c
GtkDropTarget *dt = gtk_drop_target_new(G_TYPE_FILE, GDK_ACTION_COPY);
gtk_drop_target_set_gtypes(dt, (GType[1]){ G_TYPE_FILE }, 1);
g_signal_connect(dt, "drop", G_CALLBACK(on_files_dropped), w);
gtk_widget_add_controller(w->file_listbox, GTK_EVENT_CONTROLLER(dt));
```

In the `drop` handler, iterate the incoming `GListModel` of `GFile`s, resolve each to
a path with `g_file_get_path()`, skip directories (or recurse into them), and append to
the file list exactly as `on_file_chooser_response` does. Also set a visual drop
highlight (CSS `:drop(active)` pseudo-class) so the user sees the drop target.

---

## High

### H1. Deprecated GTK3-era widgets (warnings deliberately suppressed)

The build suppresses all deprecation warnings via `-Wno-deprecated-declarations`
(`src/gui/CMakeLists.txt:50,64`; commit `921a6a7`). This hides heavy use of
GTK3-style widgets that are deprecated in GTK4:

| Used (deprecated)                         | GTK4 replacement           | Where                                  |
|-------------------------------------------|----------------------------|----------------------------------------|
| `GtkComboBox` / `GtkComboBoxText`         | `GtkDropDown` + `GtkStringList` | every combo in `create_main_window` |
| `gtk_combo_box_text_get_active_text()`    | `gtk_drop_down_get_selected_item()` | `gui_window.c:927`             |
| `gtk_combo_box_text_append_text()`         | add to `GtkStringList`     | `populate_codec_combo`, etc.           |
| `GtkDialog` / `gtk_dialog_new_with_buttons` | `GtkWindow` + `GtkHeaderBar` | `prompt_m4v_options`, file choosers  |
| `GTK_DIALOG_MODAL`                         | (set `modal` property)     | `gui_window.c:618`                     |

`GtkDropDown` is also the widget that renders correctly under libadwaita and respects
the color scheme, so fixing C3 and this go together.

**Fix:** Migrate combos to `GtkDropDown` (one-time refactor of
`populate_codec_combo`, `populate_vulkan_device_combo`, and `get_dropdown_text`).
Replace the file-chooser dialogs and the M4V options dialog with `GtkWindow`-based
dialogs using `GtkHeaderBar`. Then remove `-Wno-deprecated-declarations` so future
regressions are caught.

---

### H2. Dead GTK3 glade file

`src/ffmpeg_convert.glade` is a GTK3 file (`<requires lib="gtk+" version="3.24"/>`)
that is **never loaded** — no `GtkBuilder` call exists anywhere (confirmed by search).
The entire UI is built imperatively in `create_main_window()`. The file is dead weight
and its GTK3 requirement would break if ever loaded in a GTK4 app.

**Fix:** Delete `src/ffmpeg_convert.glade`. (If a UI definition is desired later,
author a GTK4 `.ui` file and load with `GtkBuilder`, but that is a separate decision.)

---

### H3. Memory leak in file-chooser dialog setup

In `on_add_files_clicked` (`src/gui/gui_window.c:697-698`):

```c
gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
                                    g_file_new_for_path(g_get_home_dir()), NULL);
```

The `GFile` is created and passed in but **never `g_object_unref`'d** — leaked every
time the dialog opens.

**Fix:**
```c
GFile *home = g_file_new_for_path(g_get_home_dir());
gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), home, NULL);
g_object_unref(home);
```

---

### H4. `g_widgets` global singleton — data race on shutdown

`g_widgets` (`src/gui/gui_callbacks.c:54`) is a file-global pointer shared between the
worker callbacks and the main thread. It is written **without holding the lock** in the
worker (`gui_callbacks.c:473` and `:537`: `g_widgets = w;`) but set to `NULL` **under
the lock** during shutdown (`gui_callbacks.c:706`). This is a data race: a callback
firing on the worker thread right at shutdown can read a `g_widgets` that is being
nulled, or set `g_widgets = w` after shutdown has nulled it.

**Fix:** Pass `AppWidgets *w` through the converter callbacks via the converter's
user-data mechanism rather than a global. Alternatively, route all `g_widgets` reads
through the lock — but the cleaner design is to remove the global entirely and capture
`w` in a closure struct passed as the converter's user data (the callbacks currently
ignore any user_data). Since only one window exists this is low-impact today, but it is
a latent crash source.

---

## Medium

### M1. Layout does not reflow — fixed 6-column grid

`create_main_window` packs everything into a single `GtkGrid` with 6 fixed columns
(`src/gui/gui_window.c:341-405`). The grid does not reflow on resize: shrinking the
window clips controls; widening it leaves dead space. Long file paths in the listbox
labels have no `max-width-chars` / ellipsization set, so they force the window to grow
horizontally (`gui_window.c:513`).

**Fix:** Group controls into `GtkBox` sections (a controls bar, a file-list pane, an
action bar, a log pane) inside a vertical `GtkBox`, and let the file list and log be
`vexpand`/`hexpand`. Set `gtk_label_set_ellipsize(GTK_LABEL(path_label),
PANGO_ELLIPSIZE_MIDDLE)` and `gtk_label_set_max_width_chars()` so long paths truncate
gracefully with a tooltip showing the full path.

---

### M2. No empty-state / file-count feedback

The file `GtkListBox` shows nothing when empty — no placeholder, no count. The user
gets no visual confirmation that files were added beyond the list growing.

**Fix:** Use `gtk_list_box_set_placeholder()` to show a "Drag files here or click
'Add files…'" message when empty. Add a small label showing "N files" that updates on
add/remove.

---

### M3. Progress shows per-file only; no overall progress

`on_progress_encode` / `on_progress_analysis` update the single progress bar to the
*current file's* percentage (`src/gui/gui_callbacks.c:335`). For multi-file batches
there is no indication of overall progress (e.g. file 3 of 10).

**Fix:** Compute overall fraction as `(completed_files + current_file_fraction) /
total_files` in `update_progress_idle`, and show "File 3/10 — 45%" in the bar text.

---

### M4. No "open output folder" action after completion

On success, `finish_conversion_idle` clears the file list and shows "Completed"
(`src/gui/gui_callbacks.c:380-384`) but offers no way to open the output directory.
The user must navigate there manually.

**Fix:** Resolve the effective output dir (the code already has
`resolve_effective_output_dir`) and add a button / action that calls
`g_app_info_launch_default_for_uri()` on the output folder, shown only on successful
completion.

---

### M5. Duplicated `codec_is_mux` definition

`codec_is_mux()` is defined **twice** with different implementations:
- `src/gui/gui_window.c:60` — uses `g_strcmp0`.
- `src/gui/gui_callbacks.c:56` — uses `strcmp` with a NULL guard.

**Fix:** Define once in a shared header (`gui_window.h`) and remove the duplicate.

---

### M6. Dead compatibility stubs

`on_add_files_response` (`gui_window.c:704`) and `on_output_dir_response`
(`gui_window.c:750`) are empty stubs labelled "deprecated, kept for compatibility" but
are never connected to any signal. They are dead code.

**Fix:** Delete both functions and their forward declarations.

---

### M7. Remove selected — dangling pointer ordering

`on_remove_file_clicked` (`gui_window.c:758`) calls
`g_ptr_array_remove(w->file_paths, path)` which frees the `path` string (the array was
created with `g_free` as destroy func, `gui_window.c:297`), **then** removes the listbox
row whose child label still holds that same pointer via `g_object_set_data`
(`gui_window.c:517`). The row is destroyed immediately after, so this is unlikely to
be read-after-free in practice, but the ordering is unsafe.

**Fix:** Remove the listbox row first, then remove from the array; or store the path
copy only in the array and look it up by row index rather than carrying a second
pointer in the label.

---

## Low

### L1. No keyboard shortcuts / mnemonics

Buttons use plain labels (e.g. `"Start"`, `"Stop"`) with no `_Start` mnemonic and no
accelerator map. There are no Ctrl+ shortcuts (e.g. Ctrl+O to add files, Ctrl+Enter
to start, Escape to stop). Power users of a media converter expect these.

**Fix:** Add `_Start` / `_Stop` mnemonics to labels and an `GtkShortcutController` with
`GtkShortcut`/`GtkSignalAction` entries for the common actions.

---

### L2. `GSK_RENDERER=cairo` set only on macOS

`gui_main.c:22` forces the Cairo renderer on Apple (`#ifdef __APPLE__`) but does
nothing on Linux. If the freeze were renderer-related (it is not — see C1), this would
be the lever. No action needed unless a specific Linux GPU/driver shows NGL bugs; leave
as-is and note for diagnostics.

---

### L3. Progress-idle flood potential

Each `on_progress_encode` / `on_progress_analysis` / `on_message` call allocates a heap
struct and calls `g_idle_add_full` (`gui_callbacks.c:236-279`). ffmpeg's `-progress`
typically emits every 0.5 s, so this is normally fine, but there is **no throttling or
coalescing** — a burst of messages could queue dozens of idle handlers. The log
autoscroll also creates + deletes a `GtkTextMark` per line (`gui_callbacks.c:304-306`).

**Fix (optional):** Coalesce progress updates: keep the latest `ProgressUpdateData` in
the struct and use a single recurring `g_timeout_add(100, …)` that flushes it, rather
than one `g_idle_add` per callback. For the log, reuse a persistent mark instead of
create/delete per line.

---

### L4. No tooltips on technical options

Codec profiles, deblock, audio modes, and the Vulkan device selector have no tooltips.
An agent/user unfamiliar with the tool cannot discover what "4444" or "fdk_aac_320_ac3_640"
mean without reading the README.

**Fix:** `gtk_widget_set_tooltip_text()` on each control with a one-line explanation.

---

## Suggested fix order

1. **C1** (thread the probe) — fixes the headline freeze; biggest user-visible win.
2. **C2** (collect options on main thread) — correctness; pairs naturally with C1
   threading changes.
3. **C4** (drag-and-drop) — high-value usability, self-contained, restores parity
   with the Pascal GUI.
4. **C3** (theme/CSS) — best done together with **H1** (migrate to `GtkDropDown` +
   libadwaita), since both touch every combo and the window shell.
5. **H2–H4, M5–M7** — quick cleanups (dead file, leaks, duplicates) that can be done
   in one pass.
6. **M1–M4, L1, L4** — layout and UX polish.

---

## Cross-implementation note

Per `AGENTS.md`, the C and Pascal ports must stay feature-matched. The Pascal GUI
already supports drag-and-drop (`fpc/CHANGELOG.md`), so implementing **C4** closes a
parity gap rather than introducing one. The other GUI fixes (theming, layout,
deprecated widgets) are C-GTK4-specific and have no Pascal counterpart to mirror.

kilo -s ses_055614ec6ffeTWS0d2qG2QSfBU
