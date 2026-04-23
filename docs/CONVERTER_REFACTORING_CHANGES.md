# CONVERTER_REFACTORING_CHANGES.md

Registry of all changes required to refactor `src/converter/converter.c`
into a modular, platform-separated architecture as described in
`docs/CONVERTER_REFACTORING_PLAN.md`.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Changes in converter.c](#2-changes-in-converterc)
   - 2.1 [Removed Includes](#21-removed-includes)
   - 2.2 [Removed Functions (move to platform files)](#22-removed-functions-move-to-platform-files)
   - 2.3 [Refactored Functions](#23-refactored-functions)
   - 2.4 [New Includes](#24-new-includes)
   - 2.5 [Line-by-Line Changes](#25-line-by-line-changes)
3. [New Files to Create](#3-new-files-to-create)
   - 3.1 [converter_platform.h](#31-converter_platformh)
   - 3.2 [converter_common.c](#32-converter_commonc)
   - 3.3 [converter_common.h](#33-converter_commonh)
   - 3.4 [platform/converter_windows.c](#34-platformconverter_windowsc)
   - 3.5 [platform/converter_linux.c](#35-platformconverter_linuxc)
   - 3.6 [platform/converter_macos.c](#36-platformconverter_macosc)
4. [CMakeLists.txt Changes](#4-cmakeliststxt-changes)
5. [New Error Codes](#5-new-error-codes)
6. [ConvertOptions Structure Changes](#6-convertoptions-structure-changes)
7. [Immutable Guarantees](#7-immutable-guarantees)
8. [Thread Safety Improvements](#8-thread-safety-improvements)
9. [Summary Table](#9-summary-table)
10. [Validation Checklist](#10-validation-checklist)

---

## 1. Overview

### 1.1 Summary of All Changes

This document is the **implementation change registry** for the refactoring
described in `CONVERTER_REFACTORING_PLAN.md`. It lists every file that must
be modified or created, every function that must be moved or changed, and
every line-level change required.

**Scope:**

| Change Type | Count |
|-------------|-------|
| Includes removed from converter.c | 5 |
| Functions removed from converter.c (moved to platform files) | 10 |
| Functions deleted (dead code) | 1 |
| Functions refactored in converter.c | 10 |
| New files created | 6 |
| CMakeLists.txt modified | 1 |
| New error codes added | 7 |
| New fields in ConvertOptions | 1 |
| New fields in Converter struct | 5 |

### 1.2 Severity and Impact of Changes

| Change | Severity | Risk | Breaking? |
|--------|----------|------|-----------|
| Remove platform `#include` from converter.c | LOW | Low | No — compile-time only |
| Move `get_exe_dir()` to platform files | MEDIUM | Medium | No — internal function |
| Move `mkdir_p()` to platform files | MEDIUM | Medium | No — internal function |
| Rename `ERR_POPEN_FAILED` → `ERR_SUBPROCESS_START_FAILED` | HIGH | High | **YES** — public API |
| Add `platform_init()` call in `converter_create()` | MEDIUM | Medium | No |
| Delete `format_eta()` | LOW | Low | No — dead code |
| Add new error codes | LOW | Low | No — additive |
| Create `converter_platform.h` | LOW | Low | No — additive |
| Add `converter_common.c` | LOW | Low | No — additive |
| Windows platform file (new code) | HIGH | High | No — new platform only |

---

## 2. Changes in converter.c

### 2.1 Removed Includes

The following `#include` directives must be removed from `converter.c`.
For each, the replacement strategy is described.

#### Remove: `#include <unistd.h>` (line 7)

```c
// BEFORE — line 7:
#include <unistd.h>

// AFTER — REMOVED
```

**Reason:** Provides `access()`, `readlink()`, `ssize_t`, `popen()`,
`pclose()`, `sysconf()` — all of which are either POSIX-only or will be
replaced by `platform_*()` calls.

**Replacement strategy:**
- `access(path, X_OK)` in `is_executable()` → function moved to platform files
- `readlink("/proc/self/exe")` in `get_exe_dir()` → function moved to platform files
- `ssize_t` type → no longer needed (removed with `get_exe_dir()`)
- `popen()` / `pclose()` → these are available on all platforms via `<stdio.h>`
  (on Windows MSYS2 MinGW, `popen` is provided by the MSYS2 runtime).
  Include `<stdio.h>` (already present) — no change needed.
- `sysconf(_SC_NPROCESSORS_ONLN)` in `get_cpu_count()` → function moved to
  platform files

**Note:** `popen()` and `pclose()` are declared in `<stdio.h>` on POSIX
and in `<stdio.h>` on Windows MinGW. The explicit `<unistd.h>` include is
not needed for them.

---

#### Remove: `#include <libgen.h>` (line 10)

```c
// BEFORE — line 10:
#include <libgen.h>

// AFTER — REMOVED
```

**Reason:** Provides `dirname()` which is POSIX-only and not available on
Windows. Used in `get_exe_dir()` which is being moved to platform files.

**Replacement strategy:** `dirname()` will be reimplemented in each platform
file using `strrchr()` which is standard C. No replacement needed in
`converter.c`.

---

#### Remove: `#include "linux/runtime_probe.h"` (lines 11–13)

```c
// BEFORE — lines 11–13:
#if defined(__linux__)
#include "linux/runtime_probe.h"
#endif

// AFTER — REMOVED (entire block)
```

**Reason:** This header provides Linux-specific functions (`linux_get_preferred_ffmpeg_bin`,
`linux_probe_codec_support`, `LinuxCodecSupport`) that will be called from
`platform/converter_linux.c` instead of directly from `converter.c`.

**Replacement strategy:** `platform/converter_linux.c` includes this header
internally. `converter.c` calls `platform_get_ffmpeg_bin()` and
`platform_detect_gpu_support()` via the abstraction layer.

---

#### Remove: `#include <mach-o/dyld.h>` (lines 18–21, part of the `__APPLE__` block)

```c
// BEFORE — lines 18–21:
#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <mach-o/dyld.h>
#endif

// AFTER — REMOVED (entire block)
```

**Reason:** These headers are macOS-only and will be moved to
`platform/converter_macos.c`.

**Replacement strategy:**
- `_NSGetExecutablePath()` from `<mach-o/dyld.h>` — used in `get_exe_dir()`
  macOS branch, moved to `macos_get_exe_dir()` in `converter_macos.c`
- `sysctlbyname()` from `<sys/sysctl.h>` — used in `get_cpu_count()` macOS
  branch, moved to `macos_get_cpu_count()` in `converter_macos.c`

---

#### Remove: `#include <math.h>` (lines 14–16, part of `__APPLE__` block)

```c
// BEFORE — lines 14–16:
#if defined(__APPLE__)
#include <math.h>
#endif

// AFTER — REMOVED
```

**Reason:** `<math.h>` is only needed for `pow()` in `calc_hevc_vt_bitrate_kbps()`,
which is being moved to `platform/converter_macos.c`.

**Replacement strategy:** `converter_macos.c` includes `<math.h>` internally.

---

### 2.2 Removed Functions (move to platform files)

#### Move: `is_executable()` — lines 154–156

```c
// BEFORE — lines 154–156:
static int is_executable(const char* path) {
    return (access(path, X_OK) == 0);
}

// AFTER — REMOVED from converter.c
```

**Move to:** Each platform file implements the equivalent logic:
- Linux/macOS: `access(path, X_OK) == 0`
- Windows: check if file exists AND has `.exe` extension, OR use
  `PathFileExistsA()` from `<shlwapi.h>`

**Used by:** `resolve_bundled_bin()` — also being moved to platform files.

---

#### Move: `get_exe_dir()` — lines 158–185

```c
// BEFORE — lines 158–185:
static const char* get_exe_dir(void) {
    static char exe_dir[1024] = {0};
    static int initialized = 0;
    if (initialized) return exe_dir;
#if defined(__APPLE__)
    /* ... _NSGetExecutablePath branch ... */
#else
    /* ... readlink /proc/self/exe branch (Linux) ... */
#endif
    initialized = 1;
    return exe_dir;
}

// AFTER — REMOVED from converter.c
```

**Split into:**
- `platform/converter_linux.c`: `linux_get_exe_dir()` — uses `readlink("/proc/self/exe")`
- `platform/converter_macos.c`: `macos_get_exe_dir()` — uses `_NSGetExecutablePath()`
- `platform/converter_windows.c`: `windows_get_exe_dir()` — uses `GetModuleFileNameW()`

**Note:** The `#else` branch in the original code is Linux-specific
(uses `readlink`). Windows was never handled correctly.

---

#### Move: `resolve_bundled_bin()` — lines 187–201

```c
// BEFORE — lines 187–201:
static const char* resolve_bundled_bin(const char* name) {
    const char* exe_dir = get_exe_dir();
    if (exe_dir[0] == '\0') return NULL;
    static char path[1024];
    snprintf(path, sizeof(path), "%s/%s", exe_dir, name);
    if (is_executable(path)) return path;
#if defined(__APPLE__)
    snprintf(path, sizeof(path), "%s/../Resources/bin/%s", exe_dir, name);
    if (is_executable(path)) return path;
#endif
    return NULL;
}

// AFTER — REMOVED from converter.c
```

**Split into:**
- `platform/converter_linux.c`: `linux_resolve_bundled_bin()` — searches `<exe_dir>/<name>`
- `platform/converter_macos.c`: `macos_resolve_bundled_bin()` — searches `<exe_dir>/<name>`,
  then `<exe_dir>/../Resources/bin/<name>`
- `platform/converter_windows.c`: `windows_resolve_bundled_bin()` — searches `<exe_dir>\<name>.exe`

**Issues fixed in new implementations:**
- Use platform separator (not hardcoded `/`)
- Remove static buffer shared across calls (thread-safety issue)

---

#### Move: `get_cpu_count()` — lines 240–255

```c
// BEFORE — lines 240–255:
static int get_cpu_count(void) {
#if defined(__APPLE__)
    /* ... sysctlbyname branch ... */
#else
    long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);  // not on Windows!
    if (num_cpus < 1) return 1;
    return (int)num_cpus;
#endif
}

// AFTER — REMOVED from converter.c
```

**Moved to:** `converter_common.c` as a thin wrapper:
```c
int get_cpu_count(void) {
    return platform_get_cpu_count();
}
```

Platform implementations:
- Linux: `sysconf(_SC_NPROCESSORS_ONLN)` in `converter_linux.c`
- macOS: `sysctlbyname("hw.ncpu")` in `converter_macos.c`
- Windows: `GetSystemInfo().dwNumberOfProcessors` in `converter_windows.c`

---

#### Delete: `format_eta()` — lines 378–388 (DEAD CODE)

```c
// BEFORE — lines 378–388:
static void format_eta(double eta, char *buf, size_t sz) {
    if (eta <= 0) {
        snprintf(buf, sz, "ETA --:--:--");
        return;
    }
    int t = (int)eta;
    int h = t / 3600;
    int m = (t % 3600) / 60;
    int s = t % 60;
    snprintf(buf, sz, "ETA %02d:%02d:%02d", h, m, s);
}

// AFTER — DELETED COMPLETELY
```

**Reason:** This function is never called from any function in `converter.c`.
Identical implementations exist in:
- `src/platform/linux/progress.c`
- `src/platform/macos/progress.c`
- `src/platform/windows/progress.c`

The version in `converter.c` is a duplicate that was not removed when
progress reporting was moved to platform files.

---

#### Move: `mkdir_p()` — lines 393–422

```c
// BEFORE — lines 393–422:
static int mkdir_p(const char* path) {
    /* ... POSIX-specific implementation using mkdir(tmp, 0755) ... */
    /* ... only handles '/' separator ... */
}

// AFTER — REMOVED from converter.c
```

**Moved to:** Each platform file implements `platform_mkdir_recursive()`:
- Linux/macOS: Uses two-argument POSIX `mkdir(path, 0755)`, handles `/` separator
- Windows: Uses `_mkdir(path)` from `<direct.h>`, handles both `/` and `\` separators

**Issues fixed in new implementations:**
- Windows POSIX `mkdir()` with mode argument → `_mkdir()` without mode
- Windows backslash separator not handled → both separators handled

---

#### Move: `get_video_info()` — lines 496–530 (macOS only)

```c
// BEFORE — lines 496–530:
#if defined(__APPLE__)
static void get_video_info(const char *input,
                           int *out_width, int *out_height, double *out_fps) {
    /* ... ffprobe command to get video stream info ... */
}
#endif /* __APPLE__ */

// AFTER — REMOVED from converter.c
```

**Moved to:** `platform/converter_macos.c` as `macos_get_video_info()`.

The function exposes `platform_get_video_info()` in the abstraction layer.
On Linux, the implementation returns 0 (not needed for current codecs).
On Windows, the implementation is needed for future H.265 hardware encoders.

---

#### Move: `calc_hevc_vt_bitrate_kbps()` — lines 536–551 (macOS only)

```c
// BEFORE — lines 536–551:
#if defined(__APPLE__)
static int calc_hevc_vt_bitrate_kbps(int width, int height, double fps) {
    /* ... sub-linear bits-per-pixel formula ... */
}
#endif /* __APPLE__ */

// AFTER — REMOVED from converter.c
```

**Moved to:** `platform/converter_macos.c` as a `static` helper function
called internally by `macos_get_video_codec_flags()`. The formula must not
be changed:

```
base = 35000 kbps @ 4K (3840×2160) / 24 fps
bitrate = base × (pixels / base_pixels) × (fps / base_fps)^0.75
clamped to [2000, 80000] kbps
```

---

### 2.3 Refactored Functions

#### Refactor: `ffmpeg_encoder_available()` — lines 104–152

**Current issues:**
- Static state not thread-safe (`static int initialized`)
- `2>/dev/null` shell redirect hardcoded (not portable to native Windows CMD)

**Changes:**
- Replace `2>/dev/null` with `platform_get_null_device()` call:
  ```c
  snprintf(cmd, sizeof(cmd),
           "\"%s\" -hide_banner -v error -encoders 2>%s",
           get_ffmpeg_bin(), platform_get_null_device());
  ```
- Move static state to per-instance `Converter` struct fields (see Section 8)

---

#### Refactor: `get_ffmpeg_bin()` — lines 203–218

```c
// BEFORE:
static const char* get_ffmpeg_bin(void) {
    const char* v = getenv("FFMPEG");
    if (v && v[0] != '\0') return v;
    v = getenv("FFMPEG_BIN");
    if (v && v[0] != '\0') return v;
#if defined(__linux__)
    return linux_get_preferred_ffmpeg_bin();
#endif
    const char* bundled = resolve_bundled_bin("ffmpeg");
    if (bundled) return bundled;
    return "";
}

// AFTER:
static const char* get_ffmpeg_bin(void) {
    return platform_get_ffmpeg_bin();
}
```

**Note:** The env var checking (`FFMPEG`, `FFMPEG_BIN`) moves into each
platform implementation, so `converter.c`'s wrapper becomes a simple
one-liner. Alternatively, keep the env var check common here and have
`platform_get_ffmpeg_bin()` called only for platform-specific resolution.
Either approach is acceptable; the platform file must document which env
vars it checks.

---

#### Refactor: `get_ffprobe_bin()` — lines 220–235

Same pattern as `get_ffmpeg_bin()`:

```c
// AFTER:
static const char* get_ffprobe_bin(void) {
    return platform_get_ffprobe_bin();
}
```

---

#### Refactor: `converter_create()` — lines 267–270

```c
// BEFORE:
Converter* converter_create(void) {
    Converter* c = calloc(1, sizeof(Converter));
    return c;
}

// AFTER:
Converter* converter_create(void) {
    Converter* c = calloc(1, sizeof(Converter));
    if (!c) return NULL;
    if (platform_init() != 0) {
        free(c);
        return NULL;
    }
    c->platform_initialized = 1;
    c->platform_caps = platform_detect_gpu_support();
    /* Cache tool paths for thread safety */
    strncpy(c->ffmpeg_path,   platform_get_ffmpeg_bin(),   sizeof(c->ffmpeg_path) - 1);
    strncpy(c->ffprobe_path,  platform_get_ffprobe_bin(),  sizeof(c->ffprobe_path) - 1);
    strncpy(c->mkvmerge_path, platform_get_mkvmerge_bin(), sizeof(c->mkvmerge_path) - 1);
    strncpy(c->mp4box_path,   platform_get_mp4box_bin(),   sizeof(c->mp4box_path) - 1);
    return c;
}
```

---

#### Refactor: `converter_destroy()` — lines 272–275

```c
// BEFORE:
void converter_destroy(Converter* c) {
    if (!c) return;
    free(c);
}

// AFTER:
void converter_destroy(Converter* c) {
    if (!c) return;
    if (c->platform_initialized)
        platform_cleanup();
    free(c);
}
```

---

#### Refactor: `converter_set_options()` — lines 294–334

```c
// BEFORE:
ConverterError converter_set_options(Converter* c, const ConvertOptions* opts) {
#if defined(__linux__)
    LinuxCodecSupport support;
#endif
    if (!c || !opts) return ERR_INVALID_OPTIONS;
    c->opts = *opts;
    if (!audio_output_mode_valid(c->opts.audio_output_mode))
        return ERR_INVALID_OPTIONS;
#if defined(__linux__)
    if (codec_is_linux_vaapi(c->opts.codec)) {
        linux_probe_codec_support(&support);
        /* ... fill hw_device, hwaccel_enabled ... */
    }
#endif
    return ERR_OK;
}

// AFTER:
ConverterError converter_set_options(Converter* c, const ConvertOptions* opts) {
    if (!c || !opts) return ERR_INVALID_OPTIONS;
    c->opts = *opts;
    if (opts->audio_output_mode[0] != '\0' &&  /* non-empty: validate */
        !audio_output_mode_valid(c->opts.audio_output_mode))
        return ERR_INVALID_OPTIONS;

    /* Validate required audio filters on first call */
    if (!platform_validate_audio_filters()) {
        if (c->cb.on_error)
            c->cb.on_error("required FFmpeg audio filters not available",
                           ERR_AUDIO_FILTER_VALIDATION_FAILED);
        return ERR_AUDIO_FILTER_VALIDATION_FAILED;
    }

    /* Platform-specific codec validation */
    if (!platform_supports_codec(c->opts.codec)) {
        if (c->cb.on_error)
            c->cb.on_error("requested codec not supported on this platform",
                           ERR_INVALID_OPTIONS);
        return ERR_INVALID_OPTIONS;
    }

    /* GPU detection (fills hw_device if needed) */
    if (codec_is_vaapi(c->opts.codec) || /* renamed from codec_is_linux_vaapi */
        platform_supports_codec(c->opts.codec)) {
        /* Platform fills hw_device via platform_detect_gpu_support() */
        /* On Linux: hw_device = /dev/dri/renderD128 */
        /* On Windows: hw_device = NVENC device index */
        /* On macOS: not needed (VideoToolbox uses implicit device) */
    }

    return ERR_OK;
}
```

---

#### Refactor: `ensure_output_dir_writable()` — lines 424–464

```c
// BEFORE (key parts):
const char* home = getenv("HOME");    // ← Windows: returns NULL
if (!home || home[0] == '\0') home = ".";
/* ... */
if (mkdir_p(out_dir) != 0) { ... }   // ← Windows: POSIX mkdir fails
if (access(out_dir, W_OK) != 0) { ...}  // ← Windows: W_OK unreliable

// AFTER:
const char* home = platform_get_home_dir();  // platform-aware
/* ... */
if (platform_mkdir_recursive(out_dir) != 0) { ... }
/* access() check: replace W_OK with platform_check_dir_writable() */
/* or keep stat() check which is portable */
```

---

#### Refactor: `get_duration()` — lines 469–488

```c
// BEFORE:
snprintf(cmd, sizeof(cmd),
         "\"%s\" -v error -show_entries format=duration "
         "-of default=noprint_wrappers=1:nokey=1 \"%s\" 2>/dev/null",
         ffprobe_bin, input);

// AFTER:
snprintf(cmd, sizeof(cmd),
         "\"%s\" -v error -show_entries format=duration "
         "-of default=noprint_wrappers=1:nokey=1 \"%s\" 2>%s",
         ffprobe_bin, input, platform_get_null_device());
```

**Change:** Replace hardcoded `2>/dev/null` with `platform_get_null_device()`
which returns `"/dev/null"` on POSIX and `"nul"` on Windows.

---

#### Refactor: `make_output_name()` — lines 585–675

**Current issues:**
- Output path separator hardcoded as `/` (line 663)
- Basename extraction handles `\` via `#ifdef _WIN32` but path join does not

```c
// BEFORE (line 663):
snprintf(out, out_sz, "%s/%s", opts->output_dir, filename);

// AFTER:
char* joined = platform_join_paths(opts->output_dir, filename);
if (joined) {
    strncpy(out, joined, out_sz - 1);
    out[out_sz - 1] = '\0';
    free(joined);
} else {
    strncpy(out, filename, out_sz - 1);
    out[out_sz - 1] = '\0';
}
```

**Also change:** basename extraction (lines 594–600) to use
`platform_get_filename()` instead of `strrchr()` + `#ifdef _WIN32` block:

```c
// BEFORE (lines 594–600):
const char* slash = strrchr(input, '/');
#ifdef _WIN32
    const char* backslash = strrchr(input, '\\');
    if (backslash && (!slash || backslash > slash))
        slash = backslash;
#endif
const char* name = slash ? slash + 1 : input;

// AFTER:
const char* name = platform_get_filename(input);
```

---

#### Refactor: `build_ffmpeg_cmd()` — lines 909–1125

**Key changes:**

1. Replace hardcoded codec blocks with `platform_get_video_codec_flags()`:

```c
// BEFORE (codec-specific blocks, lines 971–1028):
if (strcmp(opts->codec, "prores") == 0 || ...) {
    /* ... build prores flags ... */
} else if (strcmp(opts->codec, "h264_vaapi") == 0) {
    strcat(cmd, "-c:v h264_vaapi -rc_mode auto ");
} /* ... etc ... */

// AFTER:
const char* vcodec_flags = platform_get_video_codec_flags(
    opts->codec, input, opts);
if (!vcodec_flags) {
    /* codec not supported on this platform */
    if (c->cb.on_error)
        c->cb.on_error("unsupported codec on this platform", ERR_GPU_NOT_SUPPORTED);
    return;
}
strcat(cmd, vcodec_flags);
strcat(cmd, " ");
```

**Note on cross-platform codec flags:**
- `prores` and `prores_ks` are cross-platform — their flags can stay in
  `converter.c` or be delegated to platform files. Recommended: keep cross-
  platform codec handling in `converter.c`, only delegate GPU/platform-specific
  codecs to platform files.

2. Fix buffer overflow risk: replace `char cmd[16384]` with bounds-checked
   concatenation:

```c
// BEFORE: unchecked strcat()
strcat(cmd, "-y ");   // no overflow check

// AFTER: bounds-checked helper
static int safe_append(char* buf, size_t* used, size_t cap, const char* s) {
    size_t len = strlen(s);
    if (*used + len + 1 >= cap) return 0;  /* overflow — +1 for null terminator */
    memcpy(buf + *used, s, len + 1);
    *used += len;
    return 1;
}
```

---

#### Refactor: `run_ffmpeg_encode_with_progress()` — lines 1130–1207

**Key changes:**

1. Fix buffer truncation: the `cmd` buffer is only 8192 bytes but `cmd_base`
   can be up to 16384 bytes:

```c
// BEFORE:
char cmd[8192];
snprintf(cmd, sizeof(cmd), "%s 2>&1", cmd_base);  // TRUNCATES!

// AFTER:
size_t base_len = strlen(cmd_base);
char* cmd = malloc(base_len + 8);  /* " 2>&1\0" = 6 bytes */
if (!cmd) { /* handle OOM */ }
snprintf(cmd, base_len + 8, "%s 2>&1", cmd_base);
/* ... use cmd ... */
free(cmd);
```

2. Add `platform_normalize_output_line()` call after `fgets()`:

```c
// BEFORE:
while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "out_time_ms=", 12) == 0) { ... }

// AFTER:
while (fgets(line, sizeof(line), fp)) {
    platform_normalize_output_line(line);  /* strip \r on Windows */
    if (strncmp(line, "out_time_ms=", 12) == 0) { ... }
```

---

#### Refactor: `converter_process_files()` — lines 1212–1380

**Key change:** Remove `#if defined(__APPLE__)` block:

```c
// BEFORE (lines 1347–1356):
c->opts.use_aac_for_h265 = codec_uses_aac_audio(c->opts.codec) ? 1 : 0;
#if defined(__APPLE__)
if (strcmp(c->opts.codec, "hevc_videotoolbox") == 0) {
    int w = 0, h = 0;
    double fps = 0.0;
    get_video_info(input, &w, &h, &fps);
    c->opts.hevc_vt_bitrate_kbps = calc_hevc_vt_bitrate_kbps(w, h, fps);
} else {
    c->opts.hevc_vt_bitrate_kbps = 0;
}
#endif

// AFTER — hevc_vt_bitrate_kbps is set inside platform_get_video_codec_flags()
// which is called from build_ffmpeg_cmd(), so no special code here:
c->opts.use_aac_for_h265 = codec_uses_aac_audio(c->opts.codec) ? 1 : 0;
/* hevc_vt_bitrate_kbps is now calculated inside platform_get_video_codec_flags()
   on macOS when codec == "hevc_videotoolbox". No explicit call needed here. */
```

---

### 2.4 New Includes

After removing platform-specific includes, `converter.c` should include:

```c
/* Standard C (keep): */
#include "converter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <jansson.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

/* New platform abstraction: */
#include "converter_platform.h"   /* NEW */
#include "converter_common.h"     /* NEW */

/* REMOVED: */
/* #include <unistd.h>                    — platform-specific */
/* #include <libgen.h>                    — platform-specific */
/* #if defined(__linux__)                 — moved to converter_linux.c */
/* #include "linux/runtime_probe.h"       — moved to converter_linux.c */
/* #if defined(__APPLE__)                 — moved to converter_macos.c */
/* #include <math.h>                      — moved to converter_macos.c */
/* #include <sys/sysctl.h>                — moved to converter_macos.c */
/* #include <mach-o/dyld.h>               — moved to converter_macos.c */
```

---

### 2.5 Line-by-Line Changes

The following is the detailed change map, ordered by line number:

| Lines | Change | Type | Details |
|-------|--------|------|---------|
| 7 | Remove `#include <unistd.h>` | Remove | Replaced by platform_*() calls |
| 10 | Remove `#include <libgen.h>` | Remove | dirname() moved to platform files |
| 11–13 | Remove `#include "linux/runtime_probe.h"` block | Remove | Moved to converter_linux.c |
| 14–16 | Remove `#if __APPLE__ / #include <math.h> / #endif` | Remove | Moved to converter_macos.c |
| 18–21 | Remove `#if __APPLE__ / <sysctl.h> / <dyld.h> / #endif` | Remove | Moved to converter_macos.c |
| After 21 | Add `#include "converter_platform.h"` | Add | New abstraction layer |
| After 21 | Add `#include "converter_common.h"` | Add | New shared utilities |
| 23–27 | Extend `Converter` struct with new fields | Modify | See Section 8 |
| 104–152 | Refactor `ffmpeg_encoder_available()` | Modify | Use `platform_get_null_device()` |
| 154–156 | Remove `is_executable()` | Remove | Moved to platform files |
| 158–185 | Remove `get_exe_dir()` | Remove | Moved to platform files |
| 187–201 | Remove `resolve_bundled_bin()` | Remove | Moved to platform files |
| 203–218 | Replace `get_ffmpeg_bin()` body | Modify | `return platform_get_ffmpeg_bin()` |
| 220–235 | Replace `get_ffprobe_bin()` body | Modify | `return platform_get_ffprobe_bin()` |
| 240–255 | Remove `get_cpu_count()` | Remove | Moved to converter_common.c |
| 257–262 | Move `get_filter_threads()` | Move | To converter_common.c |
| 267–270 | Refactor `converter_create()` | Modify | Add `platform_init()` call |
| 272–275 | Refactor `converter_destroy()` | Modify | Add `platform_cleanup()` call |
| 294–334 | Refactor `converter_set_options()` | Modify | Remove `#if __linux__`, add platform_*() calls |
| 347–364 | Expand `converter_error_string()` | Modify | Add new error codes |
| 369–376 | Move `parse_time_hms()` | Move | To converter_common.c |
| 378–388 | Delete `format_eta()` | Delete | Dead code |
| 393–422 | Remove `mkdir_p()` | Remove | Replaced by `platform_mkdir_recursive()` |
| 424–464 | Refactor `ensure_output_dir_writable()` | Modify | Use `platform_get_home_dir()`, `platform_mkdir_recursive()` |
| 469–488 | Refactor `get_duration()` | Modify | Replace `2>/dev/null` with `platform_get_null_device()` |
| 493 | Remove `#if defined(__APPLE__)` guard | Remove | Block below is being moved |
| 496–530 | Remove `get_video_info()` | Remove | Moved to converter_macos.c |
| 536–551 | Remove `calc_hevc_vt_bitrate_kbps()` | Remove | Moved to converter_macos.c |
| 553 | Remove `#endif /* __APPLE__ */` | Remove | Goes with the functions above |
| 585–675 | Refactor `make_output_name()` | Modify | Use `platform_get_filename()`, `platform_join_paths()` |
| 909–1125 | Refactor `build_ffmpeg_cmd()` | Modify | Use `platform_get_video_codec_flags()`, fix overflow |
| 1130–1207 | Refactor `run_ffmpeg_encode_with_progress()` | Modify | Fix buffer, add `platform_normalize_output_line()` |
| 1347–1356 | Remove `#if defined(__APPLE__)` block in loop | Remove | Video info moved to platform_get_video_codec_flags() |

---

## 3. New Files to Create

### 3.1 converter_platform.h

**Path:** `src/converter/converter_platform.h`

**Content:** See Section 6 of `CONVERTER_REFACTORING_PLAN.md` for the
complete header specification.

**Key rules for this file:**
- No `#ifdef` platform guards in this file
- No implementations — declarations only
- Include guard: `CONVERTER_PLATFORM_H`
- All functions must have documentation comments
- `PLAT_CAP_*` constants defined here

**Size estimate:** ~150 lines

---

### 3.2 converter_common.c

**Path:** `src/converter/converter_common.c`

**Content:**

```c
/* converter_common.c
 * Platform-agnostic utility functions shared across all platforms.
 */
#include "converter_common.h"
#include "converter_platform.h"
#include <string.h>
#include <stdio.h>

/* Returns the number of logical CPU cores (delegates to platform). */
int get_cpu_count(void) {
    return platform_get_cpu_count();
}

/* Returns half of CPU count, minimum 1, for FFmpeg filter threading. */
int get_filter_threads(void) {
    int cpus = get_cpu_count();
    int threads = cpus / 2;
    if (threads < 1) threads = 1;
    return threads;
}

/* Parses "HH:MM:SS.mmm" string to seconds as double.
 * Returns 0.0 if parsing fails. */
double parse_time_hms(const char *s) {
    int h = 0, m = 0;
    double sec = 0.0;
    if (sscanf(s, "%d:%d:%lf", &h, &m, &sec) == 3)
        return h * 3600.0 + m * 60.0 + sec;
    return 0.0;
}

/* Returns 1 if path is absolute on any platform, 0 if relative. */
int is_path_absolute(const char* path) {
    if (!path || path[0] == '\0') return 0;
    if (path[0] == '/') return 1;          /* POSIX */
#ifdef _WIN32
    /* Windows: "C:\..." or "\\server\..." */
    if (path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
        return 1;
    if (path[0] == '\\' && path[1] == '\\')
        return 1;
#endif
    return 0;
}
```

**Size estimate:** ~60 lines

---

### 3.3 converter_common.h

**Path:** `src/converter/converter_common.h`

**Content:**

```c
/* converter_common.h
 * Declarations for platform-agnostic utility functions.
 */
#ifndef CONVERTER_COMMON_H
#define CONVERTER_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

int    get_cpu_count(void);
int    get_filter_threads(void);
double parse_time_hms(const char *s);
int    is_path_absolute(const char* path);

#ifdef __cplusplus
}
#endif

#endif /* CONVERTER_COMMON_H */
```

**Size estimate:** ~25 lines

---

### 3.4 platform/converter_windows.c

**Path:** `src/converter/platform/converter_windows.c`

**Headers to include:**

```c
#include "../converter_platform.h"
#include <windows.h>
#include <shlwapi.h>     /* PathFindOnPathA, PathFileExistsA */
#include <direct.h>      /* _mkdir */
#include <io.h>          /* _access */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
```

**Key function implementations:**

#### `platform_init()` (Windows)

```c
int platform_init(void) {
    /* On Windows: no heavy init needed.
     * Binary resolution is done lazily or eagerly per get_*_bin() calls.
     * GPU detection is done by platform_detect_gpu_support(). */
    return 0;
}
```

#### `platform_get_ffmpeg_bin()` (Windows)

```c
static char ffmpeg_path_cache[MAX_PATH] = {0};
static int ffmpeg_path_initialized = 0;

const char* platform_get_ffmpeg_bin(void) {
    if (ffmpeg_path_initialized) return ffmpeg_path_cache;
    ffmpeg_path_initialized = 1;

    /* 1. Environment variable */
    const char* env = getenv("FFMPEG");
    if (!env || env[0] == '\0') env = getenv("FFMPEG_BIN");
    if (env && env[0] != '\0') {
        strncpy(ffmpeg_path_cache, env, MAX_PATH - 1);
        return ffmpeg_path_cache;
    }

    /* 2. Bundled next to the .exe */
    const char* exe_dir = windows_get_exe_dir();
    if (exe_dir[0] != '\0') {
        char candidate[MAX_PATH];
        snprintf(candidate, sizeof(candidate), "%s\\ffmpeg.exe", exe_dir);
        if (PathFileExistsA(candidate)) {
            strncpy(ffmpeg_path_cache, candidate, MAX_PATH - 1);
            return ffmpeg_path_cache;
        }
    }

    /* 3. Search PATH for ffmpeg.exe */
    char on_path[MAX_PATH] = "ffmpeg.exe";
    if (PathFindOnPathA(on_path, NULL)) {
        strncpy(ffmpeg_path_cache, on_path, MAX_PATH - 1);
        return ffmpeg_path_cache;
    }

    ffmpeg_path_cache[0] = '\0';
    return ffmpeg_path_cache;  /* not found — return "" */
}
```

#### `platform_get_null_device()` (Windows)

```c
const char* platform_get_null_device(void) {
    return "nul";
}
```

#### `platform_normalize_output_line()` (Windows)

```c
void platform_normalize_output_line(char* line) {
    if (!line) return;
    size_t len = strlen(line);
    /* Strip trailing \r before \n */
    if (len >= 2 && line[len-2] == '\r' && line[len-1] == '\n') {
        line[len-2] = '\n';
        line[len-1] = '\0';
    }
}
```

#### `platform_mkdir_recursive()` (Windows)

```c
int platform_mkdir_recursive(const char* path) {
    if (!path || path[0] == '\0') return -1;

    char tmp[MAX_PATH];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) { errno = ENAMETOOLONG; return -1; }

    strcpy(tmp, path);
    if (len > 1 && (tmp[len-1] == '/' || tmp[len-1] == '\\'))
        tmp[len-1] = '\0';

    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char sep = *p;
            *p = '\0';
            if (_mkdir(tmp) != 0 && errno != EEXIST) return -1;
            *p = sep;
        }
    }
    if (_mkdir(tmp) != 0 && errno != EEXIST) return -1;
    return 0;
}
```

#### `platform_get_home_dir()` (Windows)

```c
const char* platform_get_home_dir(void) {
    const char* v = getenv("USERPROFILE");
    if (v && v[0] != '\0') return v;
    v = getenv("HOMEDRIVE");
    const char* path = getenv("HOMEPATH");
    if (v && path && v[0] != '\0') {
        static char home[MAX_PATH];
        snprintf(home, sizeof(home), "%s%s", v, path);
        return home;
    }
    return ".";
}
```

#### `platform_get_cpu_count()` (Windows)

```c
int platform_get_cpu_count(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int n = (int)si.dwNumberOfProcessors;
    if (n < 1) n = 1;
    return n;
}
```

#### `platform_detect_gpu_support()` (Windows)

```c
int platform_detect_gpu_support(void) {
    int caps = 0;
    const char* ffmpeg = platform_get_ffmpeg_bin();
    if (ffmpeg[0] == '\0') return caps;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -hide_banner -v error -encoders 2>nul", ffmpeg);
    FILE* fp = popen(cmd, "r");
    if (!fp) return caps;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, " h264_nvenc"))  caps |= PLAT_CAP_NVENC_H264;
        if (strstr(line, " hevc_nvenc"))  caps |= PLAT_CAP_NVENC_HEVC;
        if (strstr(line, " h264_qsv"))    caps |= PLAT_CAP_QSV_H264;
        if (strstr(line, " hevc_qsv"))    caps |= PLAT_CAP_QSV_HEVC;
        if (strstr(line, " libfdk_aac"))  caps |= PLAT_CAP_LIBFDK_AAC;
    }
    pclose(fp);
    return caps;
}
```

**Size estimate:** ~350 lines

---

### 3.5 platform/converter_linux.c

**Path:** `src/converter/platform/converter_linux.c`

**Headers to include:**

```c
#include "../converter_platform.h"
/* runtime_probe.h is found via CMake target_include_directories */
#include "linux/runtime_probe.h"
#include <unistd.h>      /* readlink, access, sysconf */
#include <libgen.h>      /* dirname */
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
```

**Key implementations:**

#### `platform_get_null_device()` (Linux)

```c
const char* platform_get_null_device(void) {
    return "/dev/null";
}
```

#### `platform_normalize_output_line()` (Linux)

```c
void platform_normalize_output_line(char* line) {
    (void)line;  /* no-op: Linux ffmpeg outputs \n only */
}
```

#### `platform_get_ffmpeg_bin()` (Linux)

```c
const char* platform_get_ffmpeg_bin(void) {
    const char* v = getenv("FFMPEG");
    if (v && v[0] != '\0') return v;
    v = getenv("FFMPEG_BIN");
    if (v && v[0] != '\0') return v;
    return linux_get_preferred_ffmpeg_bin();
}
```

#### `platform_detect_gpu_support()` (Linux)

```c
int platform_detect_gpu_support(void) {
    LinuxCodecSupport support;
    linux_probe_codec_support(&support);
    int caps = 0;
    if (support.has_h264_vaapi) caps |= PLAT_CAP_VAAPI_H264;
    if (support.has_hevc_vaapi) caps |= PLAT_CAP_VAAPI_HEVC;
    return caps;
}
```

#### `platform_validate_audio_filters()` (Linux)

```c
int platform_validate_audio_filters(void) {
    /* Check that ffmpeg was built with --enable-libsoxr */
    const char* ffmpeg = platform_get_ffmpeg_bin();
    if (ffmpeg[0] == '\0') return 0;
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -filters 2>/dev/null | grep -q soxr", ffmpeg);
    return (system(cmd) == 0);
}
```

**Size estimate:** ~250 lines

---

### 3.6 platform/converter_macos.c

**Path:** `src/converter/platform/converter_macos.c`

**Headers to include:**

```c
#include "../converter_platform.h"
#include <mach-o/dyld.h>   /* _NSGetExecutablePath */
#include <sys/sysctl.h>    /* sysctlbyname */
#include <sys/stat.h>
#include <unistd.h>        /* access, realpath */
#include <libgen.h>        /* dirname */
#include <math.h>          /* pow */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
```

**Key implementations:**

#### `platform_get_null_device()` (macOS)

```c
const char* platform_get_null_device(void) {
    return "/dev/null";
}
```

#### `platform_normalize_output_line()` (macOS)

```c
void platform_normalize_output_line(char* line) {
    (void)line;  /* no-op: macOS ffmpeg outputs \n only */
}
```

#### `platform_get_ffmpeg_bin()` (macOS)

```c
const char* platform_get_ffmpeg_bin(void) {
    const char* v = getenv("FFMPEG");
    if (v && v[0] != '\0') return v;
    v = getenv("FFMPEG_BIN");
    if (v && v[0] != '\0') return v;

    /* MacPorts priority (per DEPENDENCIES_ANALYSIS.md) */
    if (access("/opt/local/bin/ffmpeg8", X_OK) == 0)
        return "/opt/local/bin/ffmpeg8";
    if (access("/opt/local/bin/ffmpeg", X_OK) == 0)
        return "/opt/local/bin/ffmpeg";

    /* Bundle */
    const char* bundled = macos_resolve_bundled_bin("ffmpeg");
    if (bundled) return bundled;

    return "";
}
```

#### `platform_get_video_info()` (macOS)

Moved from `get_video_info()` in converter.c with renamed signature:

```c
int platform_get_video_info(const char* input,
                             int* width, int* height, double* fps) {
    *width = 0; *height = 0; *fps = 0.0;
    /* ... ffprobe command using platform_get_ffprobe_bin() ... */
    /* Same logic as original get_video_info() */
    return (*width > 0 && *height > 0 && *fps > 0.0) ? 1 : 0;
}
```

#### `platform_get_video_codec_flags()` (macOS)

```c
const char* platform_get_video_codec_flags(const char* codec,
                                            const char* input_path,
                                            const void* opts_void) {
    const ConvertOptions* opts = (const ConvertOptions*)opts_void;
    static char flags[512];
    flags[0] = '\0';

    if (strcmp(codec, "hevc_videotoolbox") == 0) {
        int w = 0, h = 0;
        double fps = 0.0;
        platform_get_video_info(input_path, &w, &h, &fps);
        int bitrate = macos_calc_hevc_vt_bitrate_kbps(w, h, fps);
        snprintf(flags, sizeof(flags),
                 "-c:v hevc_videotoolbox -b:v %dk -tag:v hvc1 -spatial_aq 1 ",
                 bitrate > 0 ? bitrate : 35000);
        return flags;
    }
    if (strcmp(codec, "prores_videotoolbox") == 0) {
        int profile = opts ? opts->profile : 2;
        if (profile < 1 || profile > 4) profile = 2;
        snprintf(flags, sizeof(flags),
                 "-c:v prores_videotoolbox -profile:v %d -allow_sw 1 ",
                 profile);
        return flags;
    }
    return NULL;  /* not a macOS-specific codec — handled by converter.c */
}
```

**Size estimate:** ~300 lines

---

## 4. CMakeLists.txt Changes

### 4.1 File: `src/converter/CMakeLists.txt`

#### Before:

```cmake
add_library(converter STATIC
    converter.c
)
target_include_directories(converter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/platform
)
target_link_libraries(converter PUBLIC jansson_headers)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(converter PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/linux/runtime_probe.c
    )
    target_compile_definitions(converter PRIVATE
        FFMPEG_CONVERTER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
endif()
```

#### After:

```cmake
add_library(converter STATIC
    converter.c
    converter_common.c
)

target_include_directories(converter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/platform
)

target_link_libraries(converter PUBLIC jansson_headers)

# Platform-specific source files
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_sources(converter PRIVATE
        platform/converter_windows.c
    )
    target_link_libraries(converter PRIVATE
        shlwapi      # PathFindOnPathA, PathFileExistsA
        user32       # shell APIs
    )
    target_compile_definitions(converter PRIVATE
        UNICODE
        _UNICODE
    )

elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(converter PRIVATE
        platform/converter_linux.c
        ${CMAKE_SOURCE_DIR}/src/platform/linux/runtime_probe.c
    )
    target_compile_definitions(converter PRIVATE
        FFMPEG_CONVERTER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_sources(converter PRIVATE
        platform/converter_macos.c
    )
    target_link_libraries(converter PRIVATE
        "-framework CoreFoundation"
    )

endif()

# Platform header directory
target_include_directories(converter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/platform
)
```

### 4.2 Changes Summary

| Change | Reason |
|--------|--------|
| Add `converter_common.c` to base sources | Platform-agnostic utilities |
| Add `platform/converter_windows.c` for Windows | Windows implementation |
| Add `platform/converter_linux.c` for Linux | Replaces direct runtime_probe include |
| Add `platform/converter_macos.c` for Darwin | macOS implementation |
| Link `shlwapi` and `user32` on Windows | `PathFindOnPathA`, `SHGetKnownFolderPath` |
| Link `CoreFoundation` on macOS | Optional — for path utilities |

---

## 5. New Error Codes

The following changes to `ConverterError` in `converter.h` are required.

### 5.1 Renamed Error Codes (Breaking Change)

| Old Name | New Name | Reason |
|----------|----------|--------|
| `ERR_POPEN_FAILED` | `ERR_SUBPROCESS_START_FAILED` | Implementation-neutral name |
| `ERR_PCLOSE_FAILED` | `ERR_SUBPROCESS_CLOSE_FAILED` | Implementation-neutral name |

**Backward compatibility approach:** Keep old names as aliases:

```c
/* In converter.h, after enum definition: */
#define ERR_POPEN_FAILED  ERR_SUBPROCESS_START_FAILED
#define ERR_PCLOSE_FAILED ERR_SUBPROCESS_CLOSE_FAILED
```

### 5.2 New Error Codes

```c
/* Added to ConverterError enum: */

/* Platform initialization failed (platform_init() returned non-zero) */
ERR_PLATFORM_INIT_FAILED,

/* Required FFmpeg audio filter not available (e.g., libsoxr not compiled in) */
ERR_AUDIO_FILTER_VALIDATION_FAILED,

/* Requested GPU codec not available on this platform */
ERR_GPU_NOT_SUPPORTED,

/* Path exceeds platform maximum (MAX_PATH on Windows, PATH_MAX on POSIX) */
ERR_PATH_TOO_LONG,

/* platform_get_home_dir() returned empty string — cannot create default output dir */
ERR_HOME_DIR_NOT_FOUND,
```

### 5.3 Updated `converter_error_string()` Additions

```c
case ERR_SUBPROCESS_START_FAILED: return "subprocess start failed";
case ERR_SUBPROCESS_CLOSE_FAILED: return "subprocess close failed";
case ERR_PLATFORM_INIT_FAILED:    return "platform initialization failed";
case ERR_AUDIO_FILTER_VALIDATION_FAILED: return "required FFmpeg audio filter not available";
case ERR_GPU_NOT_SUPPORTED:       return "GPU codec not supported on this platform";
case ERR_PATH_TOO_LONG:           return "path exceeds maximum length";
case ERR_HOME_DIR_NOT_FOUND:      return "user home directory not found";
```

---

## 6. ConvertOptions Structure Changes

### 6.1 Existing Fields — Documentation Updates

The following existing fields have platform-specific semantics that must
be documented in `converter.h`:

```c
typedef struct {
    /* ... existing fields unchanged ... */

    /* Platform-specific GPU device identifier.
     * Linux: VAAPI render node path, e.g. "/dev/dri/renderD128"
     * Windows: NVENC/QSV device index as string, e.g. "0" or "auto"
     * macOS: not used (VideoToolbox uses implicit system device)
     * Set automatically by converter_set_options() on relevant platforms.
     */
    char hw_device[1024];

    /* HEVC VideoToolbox target bitrate in kbps.
     * macOS only. Set automatically by platform_get_video_codec_flags()
     * when codec == "hevc_videotoolbox". Set to 0 on other platforms.
     */
    int hevc_vt_bitrate_kbps;
} ConvertOptions;
```

### 6.2 New Fields in `Converter` Struct (Internal — converter.c only)

```c
struct Converter {
    ConvertOptions opts;
    ConverterCallbacks cb;
    int stop_flag;

    /* NEW: Platform state */
    int platform_initialized;  /* 1 after successful platform_init() */
    int platform_caps;         /* PLAT_CAP_* bitmask from platform_detect_gpu_support() */

    /* NEW: Cached tool paths (filled by converter_create()) */
    char ffmpeg_path[1024];    /* resolved path to ffmpeg binary */
    char ffprobe_path[1024];   /* resolved path to ffprobe binary */
    char mkvmerge_path[1024];  /* resolved path to mkvmerge binary (may be empty) */
    char mp4box_path[1024];    /* resolved path to MP4Box binary (may be empty) */
};
```

**Rationale for caching paths in the instance:**
- Thread safety: each `Converter` instance has its own copy of paths
- No shared static state between concurrent converters
- Paths are resolved once at `converter_create()` time, not lazily
- `converter_destroy()` doesn't need to free them (fixed-size char arrays)

---

## 7. Immutable Guarantees

The following functions are **PROTECTED** and must never be modified:

### 7.1 `build_audio_filter_expr()` — lines 55–95 — IMMUTABLE

```c
/* IMMUTABLE — DO NOT MODIFY
 * This function is the single source of truth for all audio filter expressions.
 * The filter parameters are calibrated and must be identical on all platforms.
 * Any change here affects the audio output of every platform.
 *
 * Protected operations:
 *   - Filter string format for each audio_norm mode
 *   - aresample parameters (resampler=soxr, precision=28, cheby=1)
 *   - loudnorm parameters (I=, TP=, LRA=, measured_* values)
 *   - volume parameters (-3dB for peak_norm)
 */
static void build_audio_filter_expr(const ConvertOptions* opts,
                                    char* filter, size_t filter_sz) { ... }
```

### 7.2 `peak_two_pass()` — lines 708–784 — IMMUTABLE

```c
/* IMMUTABLE — DO NOT MODIFY
 * This function implements the peak 2-pass loudness analysis algorithm.
 * The algorithm (volumedetect → max_volume extraction → gain calculation)
 * must produce identical results on all platforms.
 *
 * Protected operations:
 *   - FFmpeg command: -af volumedetect -f null -
 *   - max_volume string extraction
 *   - Gain formula: gain = target(-3.0) - max_volume
 */
static ConverterError peak_two_pass(Converter* c, const char* input,
                                    double* out_gain) { ... }
```

### 7.3 `loudnorm_two_pass()` — lines 796–904 — IMMUTABLE

```c
/* IMMUTABLE — DO NOT MODIFY
 * This function implements the EBU R128 loudness normalization 2-pass analysis.
 * The algorithm (loudnorm filter → JSON parsing → measured value extraction)
 * must produce identical results on all platforms.
 *
 * Protected operations:
 *   - FFmpeg command: -af "loudnorm=I=...:print_format=json" -f null -
 *   - JSON key extraction: input_i, input_tp, input_lra, input_thresh, target_offset
 *   - Parameter assignment to output pointers
 *
 * Platform adaptations permitted:
 *   - Shell redirect '2>&1' is portable and may stay as-is
 *   - popen() is available on all target platforms
 */
static ConverterError loudnorm_two_pass(
    Converter* c, const char* input,
    double I_target, double TP_target, double LRA_target,
    double* I, double* TP, double* LRA,
    double* thresh, double* offset) { ... }
```

### 7.4 Mandatory Filter Rules

The following FFmpeg filters are **required on every platform** with no
fallback allowed:

| Filter | Required By | Fallback |
|--------|-------------|---------|
| `aresample=resampler=soxr` | All audio norm modes | NONE — fail if unavailable |
| `volumedetect` | `peak_two_pass()` | NONE |
| `loudnorm` | `loudnorm_two_pass()` | NONE |
| `volume` | `build_audio_filter_expr()` peak_norm mode | NONE |
| `asplit` | Dual-audio output mode | NONE |

**The only permitted fallback** is in the AAC encoder selection chain:

```
aac_at (macOS only) → libfdk_aac → native aac
```

If `libfdk_aac` is not available, native `aac` is used. This fallback is
already implemented in `build_ffmpeg_cmd()` and must be preserved unchanged.

---

## 8. Thread Safety Improvements

### 8.1 Issues Identified

The following thread safety issues were identified in the original code
(noted in analysis document):

| Issue | Location | Problem |
|-------|----------|---------|
| Static encoder cache | `ffmpeg_encoder_available()` | Race on `initialized` and `has_*` vars |
| Static exe dir | `get_exe_dir()` | Race on `initialized` and `exe_dir` buffer |
| Static binary path | `resolve_bundled_bin()` | Race on `path` buffer |
| Plain `stop_flag` | `Converter::stop_flag` | Not atomic — data race on concurrent read/write |

### 8.2 Resolution Strategy

#### For static state in binary resolution functions:

Move all cached state into the `Converter` instance struct (see Section 6.2).
Paths are resolved once during `converter_create()` and stored per-instance.
Each call to `get_ffmpeg_bin()` returns `c->ffmpeg_path` (instance-owned).

This eliminates the static state entirely. No mutexes or atomic operations
are needed because:
- Initialization is single-threaded (inside `converter_create()`)
- After creation, paths are read-only (never written to again)
- Each converter instance is independent

#### For `stop_flag`:

The `stop_flag` field should be declared as `volatile int` at minimum, and
ideally as `_Atomic int` (C11) for guaranteed visibility across threads:

```c
/* converter.c internal struct — option A (C11): */
struct Converter {
    ConvertOptions opts;
    ConverterCallbacks cb;
    _Atomic int stop_flag;   /* C11 atomic — thread-safe read/write */
    /* ... other fields ... */
};

/* converter.c internal struct — option B (portable): */
struct Converter {
    ConvertOptions opts;
    ConverterCallbacks cb;
    volatile int stop_flag;  /* volatile — prevents compiler optimization */
    /* ... other fields ... */
};
```

Note: `volatile int` is sufficient to prevent compiler optimization of the
read, but does not guarantee memory ordering on all architectures. C11
`_Atomic int` is the correct cross-platform solution. Use `_Atomic` if the
target compilers (GCC ≥ 4.9, Clang ≥ 3.6, MSVC ≥ 2019) support C11 atomics.

#### For `ffmpeg_encoder_available()` static state:

Replace static locals with fields in the `Converter` struct:

```c
struct Converter {
    /* ... existing fields ... */
    int encoder_cache_initialized; /* NEW */
    int has_aac_at;                /* NEW */
    int has_libfdk_aac;            /* NEW */
    int has_native_aac;            /* NEW */
};
```

Initialize in `converter_create()` (all zeroed by `calloc`).
Use `c->encoder_cache_initialized` instead of `static int initialized`.

### 8.3 Functions That Remain Non-Thread-Safe

The following are inherently not thread-safe by design (same instance must
not be used from two threads simultaneously):

- `converter_set_options()` — modifies `c->opts`
- `converter_process_files()` — reads/writes `c->opts` during processing
- `converter_set_callbacks()` — modifies `c->cb`

The public API contract (documented in `LIBRARY API SPECIFICATION.md`) must
state that a single `Converter` instance must not be used from multiple threads
simultaneously. Different `Converter` instances may be used independently from
different threads.

---

## 9. Summary Table

| Function/Section | Current Location | New Location | Change Type |
|-----------------|-----------------|--------------|-------------|
| `build_audio_filter_expr()` | converter.c:55 | converter.c:55 | IMMUTABLE — unchanged |
| `peak_two_pass()` | converter.c:708 | converter.c:708 | IMMUTABLE — unchanged |
| `loudnorm_two_pass()` | converter.c:796 | converter.c:796 | IMMUTABLE — unchanged |
| `json_number_or_string_value()` | converter.c:789 | converter.c | Unchanged |
| `codec_is_linux_vaapi()` | converter.c:29 | converter.c | Rename to `codec_is_vaapi()` |
| `codec_uses_mov_container()` | converter.c:35 | converter.c | Unchanged |
| `audio_output_mode_is()` | converter.c:42 | converter.c | Unchanged |
| `audio_output_mode_valid()` | converter.c:46 | converter.c | Add null guard |
| `codec_uses_aac_audio()` | converter.c:97 | converter.c | Unchanged |
| `ffmpeg_encoder_available()` | converter.c:104 | converter.c | Refactor (fix shell redirect, static state) |
| `is_executable()` | converter.c:154 | platform/*.c | Move + platform-specific impl |
| `get_exe_dir()` | converter.c:158 | platform/*.c | Move + split by platform |
| `resolve_bundled_bin()` | converter.c:187 | platform/*.c | Move + split by platform |
| `get_ffmpeg_bin()` | converter.c:203 | converter.c (wrapper) | Replace body with platform call |
| `get_ffprobe_bin()` | converter.c:220 | converter.c (wrapper) | Replace body with platform call |
| `get_cpu_count()` | converter.c:240 | converter_common.c | Move (calls platform_get_cpu_count()) |
| `get_filter_threads()` | converter.c:257 | converter_common.c | Move |
| `converter_create()` | converter.c:267 | converter.c | Add platform_init() call |
| `converter_destroy()` | converter.c:272 | converter.c | Add platform_cleanup() call |
| `converter_set_callbacks()` | converter.c:280 | converter.c | Unchanged |
| `converter_set_options()` | converter.c:294 | converter.c | Remove #if __linux__, add platform calls |
| `converter_stop()` | converter.c:339 | converter.c | Unchanged |
| `converter_error_string()` | converter.c:347 | converter.c | Add new error codes |
| `parse_time_hms()` | converter.c:369 | converter_common.c | Move |
| `format_eta()` | converter.c:378 | DELETE | Dead code |
| `mkdir_p()` | converter.c:393 | platform/*.c | Move → `platform_mkdir_recursive()` |
| `ensure_output_dir_writable()` | converter.c:424 | converter.c | Refactor (platform_get_home_dir etc.) |
| `get_duration()` | converter.c:469 | converter.c | Fix `2>/dev/null` → platform_get_null_device() |
| `get_video_info()` | converter.c:496 | platform/converter_macos.c | Move (macOS only) |
| `calc_hevc_vt_bitrate_kbps()` | converter.c:536 | platform/converter_macos.c | Move (macOS only, static) |
| `check_file()` | converter.c:558 | converter.c | Unchanged (stat() portable) |
| `make_output_name()` | converter.c:585 | converter.c | Refactor (platform_join_paths etc.) |
| `converter_make_output_name()` | converter.c:677 | converter.c | Unchanged |
| `check_output_exists()` | converter.c:688 | converter.c | Unchanged |
| `build_ffmpeg_cmd()` | converter.c:909 | converter.c | Refactor (platform_get_video_codec_flags, fix overflow) |
| `run_ffmpeg_encode_with_progress()` | converter.c:1130 | converter.c | Fix buffer, add platform_normalize_output_line() |
| `converter_process_files()` | converter.c:1212 | converter.c | Remove #if __APPLE__ block |
| — | — | converter_platform.h (NEW) | Platform abstraction interface |
| — | — | converter_common.c (NEW) | Shared utilities |
| — | — | converter_common.h (NEW) | Declarations |
| — | — | platform/converter_windows.c (NEW) | Windows implementation |
| — | — | platform/converter_linux.c (NEW) | Linux implementation |
| — | — | platform/converter_macos.c (NEW) | macOS implementation |

---

## 10. Validation Checklist

Use this checklist to verify the refactoring is complete before declaring
Phase 4 done.

### 10.1 converter.c Cleanliness

- [ ] No `#include <unistd.h>` in converter.c
- [ ] No `#include <libgen.h>` in converter.c
- [ ] No `#include "linux/runtime_probe.h"` in converter.c
- [ ] No `#include <mach-o/dyld.h>` in converter.c
- [ ] No `#include <sys/sysctl.h>` in converter.c
- [ ] No `#include <math.h>` in converter.c (unless needed for non-platform purposes)
- [ ] All platform `#ifdef`/`#if defined(__linux__)` blocks removed from converter.c
      (exception: `#ifdef _WIN32` for path separator in string handling if needed)
- [ ] `format_eta()` deleted from converter.c
- [ ] `get_exe_dir()` removed from converter.c
- [ ] `resolve_bundled_bin()` removed from converter.c
- [ ] `mkdir_p()` removed from converter.c
- [ ] `get_video_info()` removed from converter.c
- [ ] `calc_hevc_vt_bitrate_kbps()` removed from converter.c
- [ ] `is_executable()` removed from converter.c

### 10.2 Platform Abstraction Layer

- [ ] `converter_platform.h` exists with all declared functions
- [ ] No `#ifdef` platform guards in `converter_platform.h`
- [ ] All `PLAT_CAP_*` constants defined in `converter_platform.h`
- [ ] `platform/converter_linux.c` implements all `platform_*()` functions
- [ ] `platform/converter_macos.c` implements all `platform_*()` functions
- [ ] `platform/converter_windows.c` implements all `platform_*()` functions
- [ ] `converter_common.c` and `converter_common.h` created

### 10.3 Audio Algorithm Protection

- [ ] `build_audio_filter_expr()` body unchanged from original
- [ ] `peak_two_pass()` algorithm unchanged from original (only shell redirect fixed)
- [ ] `loudnorm_two_pass()` algorithm unchanged from original
- [ ] All audio filter expressions in `build_audio_filter_expr()` intact
- [ ] `aresample=resampler=soxr:precision=28:cheby=1` unchanged
- [ ] `loudnorm` parameter format unchanged
- [ ] `volumedetect` usage in `peak_two_pass()` unchanged
- [ ] `libfdk_aac → native aac` fallback chain preserved

### 10.4 Error Codes

- [ ] `ERR_PLATFORM_INIT_FAILED` added to `ConverterError`
- [ ] `ERR_AUDIO_FILTER_VALIDATION_FAILED` added
- [ ] `ERR_GPU_NOT_SUPPORTED` added
- [ ] `ERR_PATH_TOO_LONG` added
- [ ] `ERR_HOME_DIR_NOT_FOUND` added
- [ ] `ERR_SUBPROCESS_START_FAILED` added (or `ERR_POPEN_FAILED` renamed)
- [ ] `ERR_SUBPROCESS_CLOSE_FAILED` added (or `ERR_PCLOSE_FAILED` renamed)
- [ ] All new codes handled in `converter_error_string()`
- [ ] Backward compatibility aliases added if needed

### 10.5 CMakeLists.txt

- [ ] `converter_common.c` in base sources
- [ ] `platform/converter_windows.c` added for Windows builds
- [ ] `platform/converter_linux.c` added for Linux builds
- [ ] `platform/converter_macos.c` added for macOS builds
- [ ] `shlwapi` and `user32` linked for Windows
- [ ] `CoreFoundation` linked for macOS (if needed)
- [ ] Platform include directories configured

### 10.6 Thread Safety

- [ ] Static state removed from `get_exe_dir()` (moved to instance)
- [ ] Static state removed from `resolve_bundled_bin()` (moved to instance)
- [ ] Static state removed from `ffmpeg_encoder_available()` (moved to instance)
- [ ] `stop_flag` declared as `volatile int` or `_Atomic int`
- [ ] `converter_create()` caches tool paths in instance fields
- [ ] No shared static buffers in platform files for binary path storage
      (each platform uses either static with thread-local semantics, or
      instance-based storage)

### 10.7 No Static State in Platform Functions

- [ ] `platform_get_ffmpeg_bin()` on Windows: if using static cache, document
      thread safety assumptions (acceptable for read-after-init pattern)
- [ ] `platform_get_exe_dir()`: static init cache is acceptable if initialization
      is guaranteed single-threaded (inside `converter_create()`)
- [ ] No writable static buffers shared across concurrent calls

### 10.8 Build Verification

- [ ] Code compiles on Linux without warnings (`-Wall -Wextra`)
- [ ] Code compiles on macOS without warnings
- [ ] Code compiles on Windows (MSYS2 MinGW x64) without warnings
- [ ] No POSIX headers included unconditionally in converter.c
- [ ] No Windows headers included unconditionally in converter.c

### 10.9 Functional Verification

- [ ] `peak_two_pass()` produces same gain value before and after refactoring
- [ ] `loudnorm_two_pass()` produces same JSON-extracted values
- [ ] `build_ffmpeg_cmd()` produces identical command strings for each codec
      before and after refactoring
- [ ] Output file produced by refactored code is binary-identical (or
      functionally equivalent) to output produced by original code

---

*This document is the implementation registry for `docs/CONVERTER_REFACTORING_PLAN.md`.
Both documents were generated based on analysis in `docs/CONVERTER_CODE_ANALYSIS.md`
and `docs/DEPENDENCIES_ANALYSIS.md`.*
