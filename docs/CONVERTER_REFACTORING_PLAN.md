# CONVERTER_REFACTORING_PLAN.md

Architectural refactoring plan for `src/converter/converter.c`: separating
platform-specific code from common logic and defining a new platform
abstraction layer.

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Current Architecture Analysis](#2-current-architecture-analysis)
3. [Proposed New File Structure](#3-proposed-new-file-structure)
4. [Component Responsibilities](#4-component-responsibilities)
5. [Function Distribution by Category](#5-function-distribution-by-category)
6. [New Platform Abstraction Layer: converter_platform.h](#6-new-platform-abstraction-layer-converter_platformh)
7. [CMakeLists.txt Changes](#7-cmakeliststxt-changes)
8. [Data Structure Changes](#8-data-structure-changes)
9. [Key Design Decisions](#9-key-design-decisions)
10. [Implementation Phases](#10-implementation-phases)

---

## 1. Executive Summary

### 1.1 Current State Analysis

`converter.c` is a ~1,381-line monolithic C file (as of the initial analysis
commit) that implements the entire
video/audio conversion pipeline. It compiles and runs correctly on **Linux**
and **macOS** but **will not compile on Windows** without MSYS2's POSIX
compatibility layer due to hard dependencies on POSIX-specific headers
and functions.

**Key problems with the current structure:**

| Severity | Count | Category |
|----------|-------|----------|
| CRITICAL | 6 | Windows compilation blockers |
| HIGH | 9 | Windows runtime failures |
| MEDIUM | 7 | Cross-platform portability gaps |
| — | 3 | Audio processing functions (immutable, correct) |

The file currently mixes three distinct concerns:
- **Common conversion logic**: audio filter building, loudnorm 2-pass,
  peak 2-pass, ffmpeg command construction, progress parsing
- **Linux-specific code**: VAAPI detection, `/proc/self/exe` exe resolution,
  `sysconf()`, `linux_get_preferred_ffmpeg_bin()`
- **macOS-specific code**: `_NSGetExecutablePath()`, `sysctlbyname()`,
  `get_video_info()`, `calc_hevc_vt_bitrate_kbps()`

Windows-specific code is almost entirely absent: only one `#ifdef _WIN32`
block exists (for backslash handling in `make_output_name()`). Everything
else that should be Windows-specific is simply broken or missing.

### 1.2 Proposed New Architecture

The refactoring introduces a **thin platform abstraction layer** between the
common converter logic and platform-specific implementations:

```
converter.c  →  converter_platform.h  →  platform/{linux,macos,windows}.c
```

The platform abstraction layer (`converter_platform.h`) defines a uniform
C interface for all platform-specific operations: binary resolution, path
handling, CPU detection, GPU detection, and output normalization. Each
platform implements this interface in its own source file.

The common code in `converter.c` never calls platform-specific functions
directly — it calls only `platform_*()` functions defined in
`converter_platform.h`. The correct platform implementation is compiled
in via the build system (`CMakeLists.txt`).

### 1.3 Benefits of Refactoring

| Benefit | Detail |
|---------|--------|
| **Windows compilation** | Removes all POSIX blockers from `converter.c` |
| **Windows runtime correctness** | Proper binary search, path handling, home dir |
| **Maintainability** | Platform code lives in dedicated, focused files |
| **Testability** | Each platform module can be tested in isolation |
| **Audio algorithm protection** | Common audio code is separated and protected |
| **Thread safety** | Static state moved to per-instance or platform-init phase |
| **Clear contract** | `converter_platform.h` documents the exact interface |

---

## 2. Current Architecture Analysis

### 2.1 Code Distribution (Common Logic vs. Platform-Specific)

Based on analysis of `src/converter/converter.c` (1,381 lines):

| Category | Lines | Percentage |
|----------|-------|------------|
| Common logic (platform-agnostic) | ~850 | ~62% |
| Immutable audio algorithms | ~200 | ~14% |
| Linux-specific code | ~80 | ~6% |
| macOS-specific code | ~100 | ~7% |
| Mixed code (needs refactoring) | ~150 | ~11% |

The "mixed code" category includes functions that have platform-agnostic
logic but call platform-specific helpers (e.g., `get_ffmpeg_bin()` reads
env vars — common — then delegates to `linux_get_preferred_ffmpeg_bin()` —
platform-specific).

### 2.2 Platform-Specific Dependencies

#### Linux Dependencies

| Symbol | Header | Used In | Lines |
|--------|--------|---------|-------|
| `readlink()` | `<unistd.h>` | `get_exe_dir()` | 176 |
| `ssize_t` | `<unistd.h>` | `get_exe_dir()` | 176 |
| `sysconf(_SC_NPROCESSORS_ONLN)` | `<unistd.h>` | `get_cpu_count()` | 251 |
| `linux_get_preferred_ffmpeg_bin()` | `linux/runtime_probe.h` | `get_ffmpeg_bin()` | 211 |
| `linux_get_preferred_ffprobe_bin()` | `linux/runtime_probe.h` | `get_ffprobe_bin()` | 228 |
| `linux_probe_codec_support()` | `linux/runtime_probe.h` | `converter_set_options()` | 312 |
| `LinuxCodecSupport` | `linux/runtime_probe.h` | `converter_set_options()` | 299 |

#### macOS Dependencies

| Symbol | Header | Used In | Lines |
|--------|--------|---------|-------|
| `_NSGetExecutablePath()` | `<mach-o/dyld.h>` | `get_exe_dir()` | 167 |
| `sysctlbyname("hw.ncpu")` | `<sys/sysctl.h>` | `get_cpu_count()` | 244 |
| `sysconf(_SC_NPROCESSORS_ONLN)` | `<unistd.h>` | `get_cpu_count()` | 247 |
| `pow()` | `<math.h>` | `calc_hevc_vt_bitrate_kbps()` | 546 |
| `get_video_info()` | (internal) | `converter_process_files()` | 1351 |
| `calc_hevc_vt_bitrate_kbps()` | (internal) | `converter_process_files()` | 1352 |

#### Windows Dependencies (missing — must be added)

| Required Operation | Windows API | Current State |
|-------------------|-------------|---------------|
| Get executable directory | `GetModuleFileNameW()` | MISSING — uses Linux `/proc/self/exe` |
| CPU count | `GetSystemInfo()` | MISSING — uses `sysconf()` (fails) |
| Home directory | `USERPROFILE` / `SHGetKnownFolderPath()` | MISSING — uses `HOME` (returns NULL) |
| Make directory recursive | `_mkdir()` from `<direct.h>` | BROKEN — uses 2-arg POSIX `mkdir()` |
| File executability check | Extension check (`.exe`) | BROKEN — uses `access(X_OK)` (wrong) |
| Enumerate GPU encoders | `ffmpeg -encoders` (same as others) | MISSING |

### 2.3 Current Issues and Pain Points

#### Pain Point 1: God File Anti-Pattern

All platform logic is in one 1,381-line file. Adding a Windows branch to any
function requires understanding the entire file context. There is no clear
separation of concerns.

#### Pain Point 2: Implicit Platform Detection

The file uses `#if defined(__linux__)`, `#if defined(__APPLE__)`, `#ifdef _WIN32`
scattered throughout. The `#else` branches (which should catch Windows) instead
implement Linux behavior, so Windows silently gets broken Linux code.

#### Pain Point 3: Static State Not Thread-Safe

`ffmpeg_encoder_available()` uses static local variables (`initialized`,
`has_aac_at`, etc.) that create data races in multi-threaded scenarios.
`get_exe_dir()` and `resolve_bundled_bin()` use static buffers. These need to
be moved to per-instance state or protected by platform-level initialization.

#### Pain Point 4: No Windows Binary Search

The most critical gap: on Windows, `get_ffmpeg_bin()` returns `""` because
`get_exe_dir()` fails (uses `/proc/self/exe`) and no Windows search logic
exists. Every FFmpeg operation fails silently.

#### Pain Point 5: POSIX mkdir

`mkdir_p()` uses the two-argument POSIX `mkdir(path, 0755)` which does not
compile on MSVC and does not work correctly with backslash-separated Windows
paths.

#### Pain Point 6: Dead Code

`format_eta()` (lines 378–388) in `converter.c` is never called by any
function in the file. Identical functions exist in the platform-specific
progress files (`src/platform/{linux,macos,windows}/progress.c`). The version
in `converter.c` should be deleted.

---

## 3. Proposed New File Structure

```
src/converter/
├── converter.h              (UNCHANGED — public API)
├── converter.c              (REFACTORED — common logic only, no platform #ifdefs)
├── converter_platform.h     (NEW — platform abstraction interface)
├── converter_common.c       (NEW — shared pure-C utilities)
├── converter_common.h       (NEW — declarations for converter_common.c)
└── platform/
    ├── converter_windows.c  (NEW — Windows-specific implementations)
    ├── converter_linux.c    (NEW/MOVED — Linux-specific implementations)
    └── converter_macos.c    (NEW/MOVED — macOS-specific implementations)
```

### File Roles Overview

| File | Role | Status |
|------|------|--------|
| `converter.h` | Public API — types, callbacks, error enum, function prototypes | UNCHANGED |
| `converter.c` | Main processing: loop, audio algorithms, command building | REFACTORED |
| `converter_platform.h` | Platform abstraction interface declarations | NEW |
| `converter_common.c` | Platform-agnostic utilities: CPU count, time parsing | NEW |
| `converter_common.h` | Declarations for converter_common.c | NEW |
| `platform/converter_windows.c` | Windows: binary resolution, path handling, GPU | NEW |
| `platform/converter_linux.c` | Linux: exe dir, binary resolution, CPU (already in runtime_probe.c) | NEW/MOVED |
| `platform/converter_macos.c` | macOS: bundle paths, VideoToolbox helpers | NEW/MOVED |

### What Does NOT Change

- `converter.h` — public API is stable and must not be broken
- `src/platform/linux/runtime_probe.c` — already well-structured, used via
  `converter_linux.c` wrapper
- All audio processing algorithms (`build_audio_filter_expr`, `peak_two_pass`,
  `loudnorm_two_pass`) — immutable by design rule

---

## 4. Component Responsibilities

### 4.1 converter.c — Core Engine

**Responsibility:** Implement the platform-agnostic conversion pipeline.

The refactored `converter.c` must:
- Include only standard C headers (`<stdlib.h>`, `<string.h>`, `<stdio.h>`,
  `<jansson.h>`, `<sys/stat.h>`, `<time.h>`, `<errno.h>`)
- Include `"converter_platform.h"` for all platform-specific operations
- Include `"converter_common.h"` for shared utilities
- Contain **zero** platform `#ifdef` blocks (except possibly `#ifdef _WIN32`
  for the single path-separator case in `make_output_name()`, which is
  acceptable as it is purely string logic)
- Call `platform_init()` in `converter_create()`
- Call `platform_cleanup()` in `converter_destroy()`

**Functions that stay in converter.c:**

| Function | Lines | Notes |
|----------|-------|-------|
| `codec_is_linux_vaapi()` | 29–33 | Rename to `codec_is_vaapi()`, keep common |
| `codec_uses_mov_container()` | 35–40 | Pure string — keep |
| `audio_output_mode_is()` | 42–44 | Pure string — keep |
| `audio_output_mode_valid()` | 46–53 | Pure string — keep (add null guard) |
| `build_audio_filter_expr()` | 55–95 | **IMMUTABLE** — keep |
| `codec_uses_aac_audio()` | 97–100 | Keep (describes hevc_videotoolbox behavior) |
| `ffmpeg_encoder_available()` | 104–152 | Refactor: call `platform_*` for shell redirect |
| `converter_create()` | 267–270 | Add `platform_init()` call |
| `converter_destroy()` | 272–275 | Add `platform_cleanup()` call |
| `converter_set_callbacks()` | 280–289 | Unchanged |
| `converter_set_options()` | 294–334 | Replace Linux guard with `platform_detect_gpu()` |
| `converter_stop()` | 339–342 | Unchanged |
| `converter_error_string()` | 347–364 | Expand with new error codes |
| `parse_time_hms()` | 369–376 | Move to `converter_common.c` |
| `check_file()` | 558–580 | Mostly portable — keep |
| `make_output_name()` | 585–675 | Refactor: use `platform_join_paths()`, `platform_get_filename()` |
| `converter_make_output_name()` | 677–684 | Unchanged (public wrapper) |
| `check_output_exists()` | 688–703 | Unchanged |
| `peak_two_pass()` | 708–784 | **IMMUTABLE** — keep |
| `json_number_or_string_value()` | 789–793 | Portable — keep |
| `loudnorm_two_pass()` | 796–904 | **IMMUTABLE** — keep |
| `build_ffmpeg_cmd()` | 909–1125 | Refactor: call `platform_get_video_codec_flags()` |
| `run_ffmpeg_encode_with_progress()` | 1130–1207 | Refactor: call `platform_normalize_output_line()` |
| `converter_process_files()` | 1212–1380 | Refactor: remove `#if __APPLE__` block |

**Functions removed from converter.c (moved to platform files):**

| Function | Lines | Target |
|----------|-------|--------|
| `is_executable()` | 154–156 | platform-specific |
| `get_exe_dir()` | 158–185 | platform-specific |
| `resolve_bundled_bin()` | 187–201 | platform-specific |
| `get_ffmpeg_bin()` | 203–218 | replaced by `platform_get_ffmpeg_bin()` |
| `get_ffprobe_bin()` | 220–235 | replaced by `platform_get_ffprobe_bin()` |
| `get_cpu_count()` | 240–255 | `converter_common.c` (uses platform call internally) |
| `format_eta()` | 378–388 | DELETE (dead code) |
| `mkdir_p()` | 393–422 | replaced by `platform_mkdir_recursive()` |
| `get_video_info()` | 496–530 | `platform/converter_macos.c` |
| `calc_hevc_vt_bitrate_kbps()` | 536–551 | `platform/converter_macos.c` |

### 4.2 converter_platform.h — Abstraction Layer

**Responsibility:** Define the contract between `converter.c` and
platform-specific implementations. This is a pure interface — no implementation,
no platform `#ifdef` directives.

Every function declared in `converter_platform.h` must be implemented in
exactly one of:
- `platform/converter_windows.c` (when `_WIN32` is defined)
- `platform/converter_linux.c` (when `__linux__` is defined)
- `platform/converter_macos.c` (when `__APPLE__` is defined)

The CMake build system selects the correct file. See Section 6 for the
complete interface specification.

### 4.3 converter_common.c — Shared Utilities

**Responsibility:** Implement utility functions that are platform-agnostic
but not suitable for inclusion directly in `converter.c`.

| Function | Purpose |
|----------|---------|
| `get_cpu_count()` | Calls `platform_get_cpu_count()`, caches result |
| `get_filter_threads()` | Returns `max(cpu_count/2, 1)` |
| `parse_time_hms()` | Parses `HH:MM:SS.mmm` to seconds |
| `is_path_absolute()` | Returns 1 if path starts with `/` or `X:\` |

### 4.4 platform/converter_windows.c — Windows Implementation

**Responsibility:** Implement all `platform_*()` functions for Windows,
using Win32 APIs where needed (compatible with both MSYS2 MinGW and MSVC).

Key Windows-specific operations:
- `GetModuleFileNameW()` for executable directory
- PATH search for `ffmpeg.exe`, `ffprobe.exe`, `mkvmerge.exe`, `MP4Box.exe`
- `GetSystemInfo()` for CPU count
- `USERPROFILE` / `SHGetKnownFolderPath()` for home directory
- `_mkdir()` from `<direct.h>` for directory creation
- Extension-based executability check (`.exe`)
- `ffmpeg -encoders` probe for NVENC/QSV availability
- Path separator normalization

### 4.5 platform/converter_linux.c — Linux Implementation

**Responsibility:** Implement all `platform_*()` functions for Linux,
delegating to existing `runtime_probe.c` where appropriate.

Key Linux-specific operations:
- `readlink("/proc/self/exe")` for executable directory
- `linux_get_preferred_ffmpeg_bin()` / `linux_get_preferred_ffprobe_bin()`
  (already in `runtime_probe.c`)
- `sysconf(_SC_NPROCESSORS_ONLN)` for CPU count
- VAAPI GPU detection via `linux_probe_codec_support()`
- `access(X_OK)` for executability check

### 4.6 platform/converter_macos.c — macOS Implementation

**Responsibility:** Implement all `platform_*()` functions for macOS.

Key macOS-specific operations:
- `_NSGetExecutablePath()` + `realpath()` + `dirname()` for executable directory
- `sysctlbyname("hw.ncpu")` for CPU count
- Bundle path resolution (`../Resources/bin/`)
- `get_video_info()` → `platform_get_video_info()` (ffprobe video stream probe)
- `calc_hevc_vt_bitrate_kbps()` → called inside `platform_get_video_codec_flags()`

---

## 5. Function Distribution by Category

### 5.1 STAY IN converter.c (Common Logic)

These functions contain no platform-specific code and must remain in the
common file:

```
converter_create()               — lifecycle
converter_destroy()              — lifecycle
converter_set_callbacks()        — lifecycle
converter_set_options()          — refactored: call platform_detect_gpu()
converter_stop()                 — lifecycle
converter_error_string()         — error mapping
check_file()                     — uses stat() / portable
check_output_exists()            — uses stat() / portable
make_output_name()               — refactored: use platform_join_paths()
converter_make_output_name()     — public wrapper
get_duration()                   — refactored: use platform_null_device()
codec_is_linux_vaapi()           — rename to codec_is_vaapi()
codec_uses_mov_container()       — pure string
audio_output_mode_is()           — pure string
audio_output_mode_valid()        — pure string (add null guard)
codec_uses_aac_audio()           — pure string
ffmpeg_encoder_available()       — refactored: use platform_get_null_device()
build_ffmpeg_cmd()               — refactored: call platform_get_video_codec_flags()
run_ffmpeg_encode_with_progress() — refactored: call platform_normalize_output_line()
converter_process_files()        — refactored: remove #if __APPLE__ block

[IMMUTABLE — never modify]
build_audio_filter_expr()        — audio filter expression builder
peak_two_pass()                  — peak loudness analysis
loudnorm_two_pass()              — EBU R128 loudness analysis
json_number_or_string_value()    — JSON parsing helper for loudnorm
```

### 5.2 EXTRACT TO platform/converter_windows.c

New functions implementing `platform_*()` interface for Windows:

```c
// Lifecycle
int  windows_platform_init(void);
void windows_platform_cleanup(void);

// Executable and binary resolution
const char* windows_get_exe_dir(void);
const char* windows_resolve_bundled_bin(const char* name);
const char* windows_get_ffmpeg_bin(void);
const char* windows_get_ffprobe_bin(void);
const char* windows_get_mkvmerge_bin(void);
const char* windows_get_mp4box_bin(void);
int         windows_is_executable(const char* path);

// Path operations
char*       windows_escape_path_for_command(const char* path);
int         windows_mkdir_recursive(const char* path);
const char* windows_get_home_dir(void);
const char* windows_get_filename(const char* path);
char*       windows_join_paths(const char* dir, const char* file);
int         windows_path_is_absolute(const char* path);
const char* windows_get_null_device(void);      // returns "nul"

// Output handling
void        windows_normalize_output_line(char* line);  // strip \r

// GPU and codec support
int         windows_detect_gpu_support(void);
const char* windows_get_video_codec_flags(const char* codec);
int         windows_validate_audio_filters(void);
int         windows_supports_codec(const char* codec);
int         windows_get_cpu_count(void);
int         windows_get_video_info(const char* input,
                int* width, int* height, double* fps);
```

**Windows binary search strategy** (in priority order):
1. Check `FFMPEG` / `FFMPEG_BIN` environment variables
2. Check if binary exists next to the `.exe` (bundled)
3. Search `PATH` for `ffmpeg.exe`

### 5.3 EXTRACT TO platform/converter_linux.c

New functions implementing `platform_*()` interface for Linux:

```c
// Lifecycle
int  linux_platform_init(void);
void linux_platform_cleanup(void);

// Executable and binary resolution
const char* linux_get_exe_dir(void);             // uses /proc/self/exe
const char* linux_resolve_bundled_bin(const char* name);
const char* linux_get_ffmpeg_bin(void);          // delegates to runtime_probe
const char* linux_get_ffprobe_bin(void);         // delegates to runtime_probe
const char* linux_get_mkvmerge_bin(void);
const char* linux_get_mp4box_bin(void);
int         linux_is_executable(const char* path);  // access(X_OK)

// Path operations
char*       linux_escape_path_for_command(const char* path);
int         linux_mkdir_recursive(const char* path);  // POSIX mkdir
const char* linux_get_home_dir(void);              // getenv("HOME")
const char* linux_get_filename(const char* path);  // strrchr-based
char*       linux_join_paths(const char* dir, const char* file);
int         linux_path_is_absolute(const char* path);  // starts with /
const char* linux_get_null_device(void);           // returns "/dev/null"

// Output handling
void        linux_normalize_output_line(char* line);  // no-op (already \n)

// GPU and codec support
int         linux_detect_gpu_support(void);   // VAAPI probe
const char* linux_get_video_codec_flags(const char* codec);
int         linux_validate_audio_filters(void);
int         linux_supports_codec(const char* codec);
int         linux_get_cpu_count(void);        // sysconf()
int         linux_get_video_info(const char* input,
                int* width, int* height, double* fps);  // stub (returns 0)
```

**Note:** Most binary resolution logic already exists in
`src/platform/linux/runtime_probe.c`. The new `converter_linux.c` wraps
those functions in the `platform_*()` interface.

### 5.4 EXTRACT TO platform/converter_macos.c

New functions implementing `platform_*()` interface for macOS:

```c
// Lifecycle
int  macos_platform_init(void);
void macos_platform_cleanup(void);

// Executable and binary resolution
const char* macos_get_exe_dir(void);          // _NSGetExecutablePath + realpath
const char* macos_resolve_bundled_bin(const char* name);
const char* macos_get_ffmpeg_bin(void);       // MacPorts → bundle → PATH
const char* macos_get_ffprobe_bin(void);
const char* macos_get_mkvmerge_bin(void);
const char* macos_get_mp4box_bin(void);
int         macos_is_executable(const char* path);   // access(X_OK)

// Path operations
char*       macos_escape_path_for_command(const char* path);
int         macos_mkdir_recursive(const char* path);  // POSIX mkdir
const char* macos_get_home_dir(void);                 // getenv("HOME")
const char* macos_get_filename(const char* path);     // strrchr-based
char*       macos_join_paths(const char* dir, const char* file);
int         macos_path_is_absolute(const char* path);
const char* macos_get_null_device(void);              // returns "/dev/null"

// Output handling
void        macos_normalize_output_line(char* line);  // no-op

// GPU and codec support
int         macos_detect_gpu_support(void);   // VideoToolbox availability
const char* macos_get_video_codec_flags(const char* codec);  // includes bitrate calc
int         macos_validate_audio_filters(void);
int         macos_supports_codec(const char* codec);
int         macos_get_cpu_count(void);        // sysctlbyname("hw.ncpu")
int         macos_get_video_info(const char* input,
                int* width, int* height, double* fps);  // existing get_video_info()

// Internal macOS helpers (not exposed in platform_*.h)
static int  macos_calc_hevc_vt_bitrate_kbps(int w, int h, double fps);
```

**macOS FFmpeg resolution order** (per `DEPENDENCIES_ANALYSIS.md`):
1. `FFMPEG` / `FFMPEG_BIN` environment variables
2. `/opt/local/bin/ffmpeg8` (MacPorts FFmpeg 8)
3. `/opt/local/bin/ffmpeg` (MacPorts FFmpeg any version)
4. Bundle: `<exe_dir>/../Resources/bin/ffmpeg`
5. `<exe_dir>/ffmpeg`

### 5.5 MOVE TO converter_common.c (Pure Utilities)

```c
int    get_cpu_count(void);        // calls platform_get_cpu_count()
int    get_filter_threads(void);   // max(cpu_count/2, 1)
double parse_time_hms(const char* s);   // sscanf HH:MM:SS.mmm
int    is_path_absolute(const char* p); // starts with / or X:\
```

---

## 6. New Platform Abstraction Layer: converter_platform.h

The complete interface for the platform abstraction layer. This header is
included by `converter.c` and must be implemented by exactly one platform
source file selected by the build system.

```c
/** converter_platform.h
 * Platform abstraction interface for converter.c
 * All platform-specific operations are declared here and implemented
 * in platform/converter_{linux,macos,windows}.c
 *
 * Rules:
 *  - No platform #ifdef in this file
 *  - No implementation in this file (header only)
 *  - Every function must be implemented on every supported platform
 */

#ifndef CONVERTER_PLATFORM_H
#define CONVERTER_PLATFORM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------
 *  Lifecycle
 * --------------------------------------------------------------- */

/**
 * platform_init() — Called once from converter_create().
 * Resolves binary paths, detects GPU capabilities, and caches results.
 * Returns 0 on success, non-zero on error.
 */
int platform_init(void);

/**
 * platform_cleanup() — Called from converter_destroy().
 * Releases any resources allocated by platform_init().
 */
void platform_cleanup(void);

/* ---------------------------------------------------------------
 *  Binary resolution
 * --------------------------------------------------------------- */

/**
 * Returns the path to the ffmpeg binary, or "" if not found.
 * Priority: FFMPEG env → FFMPEG_BIN env → platform search → "".
 * The returned pointer is valid for the lifetime of the process.
 */
const char* platform_get_ffmpeg_bin(void);

/**
 * Returns the path to the ffprobe binary, or "" if not found.
 */
const char* platform_get_ffprobe_bin(void);

/**
 * Returns the path to the mkvmerge binary, or "" if not found.
 */
const char* platform_get_mkvmerge_bin(void);

/**
 * Returns the path to the MP4Box binary, or "" if not found.
 */
const char* platform_get_mp4box_bin(void);

/* ---------------------------------------------------------------
 *  Path operations
 * --------------------------------------------------------------- */

/**
 * Returns a shell-escaped version of path suitable for inclusion in
 * a command string passed to popen().
 * Caller must free the returned string.
 * On POSIX: wraps in single quotes and escapes embedded single quotes.
 * On Windows: wraps in double quotes and escapes embedded double quotes.
 */
char* platform_escape_path_for_command(const char* path);

/**
 * Creates a directory and all intermediate directories.
 * Handles both '/' and '\\' separators.
 * Returns 0 on success, -1 on error (with errno set).
 */
int platform_mkdir_recursive(const char* path);

/**
 * Returns the user's home directory path.
 * Linux/macOS: getenv("HOME")
 * Windows: getenv("USERPROFILE") or SHGetKnownFolderPath(FOLDERID_Documents)
 * Never returns NULL; falls back to "." if not found.
 */
const char* platform_get_home_dir(void);

/**
 * Returns the filename component of a path (last component after separator).
 * Does not modify the input.
 * Examples: "/foo/bar.mkv" → "bar.mkv", "C:\\dir\\file.mp4" → "file.mp4"
 */
const char* platform_get_filename(const char* path);

/**
 * Joins a directory path and filename with the platform separator.
 * Caller must free the returned string.
 * Returns NULL on allocation failure.
 */
char* platform_join_paths(const char* dir, const char* file);

/**
 * Returns 1 if path is absolute, 0 if relative.
 * POSIX: starts with '/'
 * Windows: starts with drive letter + ':' or UNC '\\\\'
 */
int platform_path_is_absolute(const char* path);

/**
 * Returns the platform-specific null device string.
 * POSIX: "/dev/null"
 * Windows: "nul"
 */
const char* platform_get_null_device(void);

/* ---------------------------------------------------------------
 *  Output handling
 * --------------------------------------------------------------- */

/**
 * Normalizes a line read from popen() output.
 * On Windows: strips trailing '\\r' before '\\n'.
 * On POSIX: no-op.
 * Modifies line in-place.
 */
void platform_normalize_output_line(char* line);

/* ---------------------------------------------------------------
 *  Audio and GPU support
 * --------------------------------------------------------------- */

/**
 * Validates that all required FFmpeg audio filters are available.
 * Checks: aresample (with soxr), volumedetect, loudnorm, volume, asplit.
 * Returns 1 if all required filters are present, 0 otherwise.
 * On failure, caller should report ERR_AUDIO_FILTER_VALIDATION_FAILED.
 */
int platform_validate_audio_filters(void);

/**
 * Returns 1 if the named codec is supported on this platform.
 * Examples: "hevc_vaapi" (Linux), "hevc_videotoolbox" (macOS),
 *           "h264_nvenc" (Windows with NVIDIA GPU).
 */
int platform_supports_codec(const char* codec);

/**
 * Returns the ffmpeg video codec flags string for the given codec name.
 * This is the platform-specific portion of the -c:v argument and any
 * associated flags (e.g., "-c:v hevc_vaapi -rc_mode auto").
 * Returns NULL if codec is not supported on this platform.
 * The returned pointer is valid until the next call.
 */
const char* platform_get_video_codec_flags(const char* codec,
                                           const char* input_path,
                                           const void* opts);

/**
 * Detects GPU hardware acceleration support.
 * Returns a bitmask of available capabilities (CAP_* flags from
 * converter_platform.h).
 */
int platform_detect_gpu_support(void);

/* ---------------------------------------------------------------
 *  Utilities
 * --------------------------------------------------------------- */

/**
 * Returns the number of logical CPU cores.
 */
int platform_get_cpu_count(void);

/**
 * Probes the first video stream of input_path via ffprobe.
 * Fills width, height, fps.
 * Returns 1 on success, 0 on failure.
 * On platforms where this is not needed (Linux), returns 0 without error.
 */
int platform_get_video_info(const char* input_path,
                            int* width, int* height, double* fps);

/* ---------------------------------------------------------------
 *  Platform capability flags
 * --------------------------------------------------------------- */

#define PLAT_CAP_VAAPI_H264      (1 << 0)
#define PLAT_CAP_VAAPI_HEVC      (1 << 1)
#define PLAT_CAP_VIDEOTOOLBOX    (1 << 2)
#define PLAT_CAP_NVENC_H264      (1 << 3)
#define PLAT_CAP_NVENC_HEVC      (1 << 4)
#define PLAT_CAP_QSV_H264        (1 << 5)
#define PLAT_CAP_QSV_HEVC        (1 << 6)
#define PLAT_CAP_LIBFDK_AAC      (1 << 7)
#define PLAT_CAP_AAC_AT          (1 << 8)  /* macOS only */

#ifdef __cplusplus
}
#endif

#endif /* CONVERTER_PLATFORM_H */
```

---

## 7. CMakeLists.txt Changes

### 7.1 Current State

```cmake
# src/converter/CMakeLists.txt (current)
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

### 7.2 Proposed New CMakeLists.txt

```cmake
# src/converter/CMakeLists.txt (new)

add_library(converter STATIC
    converter.c
    converter_common.c
)

target_include_directories(converter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/platform
)

target_link_libraries(converter PUBLIC jansson_headers)

# Platform-specific source files and libraries
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_sources(converter PRIVATE
        platform/converter_windows.c
    )
    target_link_libraries(converter PRIVATE
        shlwapi    # PathFindOnPath, SHGetKnownFolderPath
        user32     # may be needed by some shell APIs
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
        "-framework CoreServices"
    )
    target_compile_options(converter PRIVATE -ObjC)

endif()

target_include_directories(converter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/platform
)
```

### 7.3 Notes on Platform Libraries

| Platform | Library | Purpose |
|----------|---------|---------|
| Windows | `shlwapi` | `PathFindOnPathA/W()` for PATH search, `PathFileExistsA()` |
| Windows | `user32` | May be required by shell API indirectly |
| macOS | `CoreFoundation` | Optional — for CFString if needed for path handling |
| macOS | `CoreServices` | Optional — FSEvents if needed |
| Linux | (none extra) | All needed functions are in libc / runtime_probe.c |

---

## 8. Data Structure Changes

### 8.1 Changes to `converter.h`

#### 8.1.1 New Error Codes

The following error codes should be added to `ConverterError`:

```c
typedef enum {
    ERR_OK = 0,

    // FILE ERRORS (unchanged)
    ERR_INPUT_NOT_FOUND,
    ERR_INPUT_NOT_REGULAR,
    ERR_INPUT_NOT_READABLE,

    // OUTPUT ERRORS (unchanged)
    ERR_OUTPUT_EXISTS,
    ERR_SKIP_FILE,

    // ANALYSIS ERRORS (unchanged)
    ERR_PEAK_ANALYSIS_FAILED,
    ERR_LOUDNORM_ANALYSIS_FAILED,

    // FFMPEG ERRORS (unchanged)
    ERR_FFMPEG_FAILED,
    ERR_FFPROBE_FAILED,

    // SYSTEM ERRORS (renamed for clarity)
    ERR_SUBPROCESS_START_FAILED,  // renamed from ERR_POPEN_FAILED
    ERR_SUBPROCESS_CLOSE_FAILED,  // renamed from ERR_PCLOSE_FAILED

    // NEW PLATFORM ERRORS
    ERR_PLATFORM_INIT_FAILED,          // platform_init() failed
    ERR_AUDIO_FILTER_VALIDATION_FAILED, // required filter not in ffmpeg
    ERR_GPU_NOT_SUPPORTED,             // requested GPU codec not available
    ERR_PATH_TOO_LONG,                 // path exceeds platform limit
    ERR_HOME_DIR_NOT_FOUND,            // platform_get_home_dir() returned empty

    // INTERNAL (unchanged)
    ERR_INVALID_OPTIONS,
    ERR_UNKNOWN
} ConverterError;
```

**Note on backward compatibility:** The renaming of `ERR_POPEN_FAILED` and
`ERR_PCLOSE_FAILED` to `ERR_SUBPROCESS_START_FAILED` and
`ERR_SUBPROCESS_CLOSE_FAILED` is a breaking API change. If the library has
external callers, add compatibility `#define` aliases:

```c
#define ERR_POPEN_FAILED  ERR_SUBPROCESS_START_FAILED
#define ERR_PCLOSE_FAILED ERR_SUBPROCESS_CLOSE_FAILED
```

#### 8.1.2 Possible ConvertOptions Changes

The following fields in `ConvertOptions` have platform semantics that need
clarification:

| Field | Current Usage | Proposed Change |
|-------|--------------|-----------------|
| `hw_device[1024]` | Linux: VAAPI render node path | Document as platform-specific. On Windows: NVENC device index or "auto". |
| `hevc_vt_bitrate_kbps` | macOS: calculated per-file | Keep, but calculate inside platform code. |
| `output_dir_status` | Never set — dead field | Add `PLATFORM_INITIALIZED` flag usage documentation. |

**Optional new field** (if platform_init() state needs to be exposed):

```c
typedef struct {
    /* ... existing fields ... */
    int _platform_caps;   /* read-only: filled by platform_init() */
} ConvertOptions;
```

### 8.2 Changes to Converter Struct (converter.c internal)

The internal `Converter` struct should be extended:

```c
struct Converter {
    ConvertOptions opts;
    ConverterCallbacks cb;
    int stop_flag;           /* existing */
    int platform_initialized; /* NEW: set by converter_create() */
    int platform_caps;        /* NEW: PLAT_CAP_* bitmask from platform_detect_gpu() */
    /* Tool paths (cached by platform_init): */
    char ffmpeg_path[1024];   /* NEW */
    char ffprobe_path[1024];  /* NEW */
    char mkvmerge_path[1024]; /* NEW */
    char mp4box_path[1024];   /* NEW */
};
```

By storing resolved paths in the `Converter` instance:
- Thread safety improves (each instance has its own paths)
- Platform files don't need static state for cached paths
- `converter_destroy()` cleans up properly

### 8.3 Changes to converter_platform.h

The `converter_platform.h` file itself introduces:
- `PLAT_CAP_*` bitmask constants (see Section 6)
- Function declarations for all platform operations

No new typedef structs are needed in the platform header — the interface uses
only primitive types and `const char*`.

---

## 9. Key Design Decisions

### 9.1 Why Separate Files Per Platform

**Decision:** One source file per platform (`converter_linux.c`,
`converter_macos.c`, `converter_windows.c`) instead of scattered `#ifdef` blocks.

**Rationale:**
- Platform files can be read, compiled, and tested independently
- No risk of one platform's change breaking another platform's code
- CMake selects the correct file — compiler never sees irrelevant code
- Windows file contains Windows-only headers (`<windows.h>`) that would
  pollute the compilation on Linux/macOS if in a common file
- macOS file contains `<mach-o/dyld.h>` and `<sys/sysctl.h>` which are
  macOS-only

### 9.2 Why These Functions Stay Common

The functions remaining in `converter.c` were chosen based on:

1. **Zero platform conditionals**: Functions that have no `#if` / `#ifdef`
   blocks and call only standard C library functions stay common.

2. **Algorithmic content**: Functions implementing audio processing algorithms
   (`build_audio_filter_expr`, `peak_two_pass`, `loudnorm_two_pass`) stay
   common because they must be **identical on all platforms** — this is a
   hard requirement for audio consistency.

3. **Main loop**: `converter_process_files()` stays common because it
   orchestrates the conversion workflow. Platform-specific steps within it
   are replaced by `platform_*()` calls.

### 9.3 Audio Algorithm Immutability Guarantee

**Rule (MANDATORY):** The following three functions are **IMMUTABLE** and
must never be modified for any reason, including platform adaptation:

| Function | Lines (current) | Algorithm |
|----------|----------------|-----------|
| `build_audio_filter_expr()` | 55–95 | Audio filter expression builder |
| `peak_two_pass()` | 708–784 | Peak loudness 2-pass analysis |
| `loudnorm_two_pass()` | 796–904 | EBU R128 loudness 2-pass analysis |

**Why immutable:**
- The audio processing result must be bit-for-bit identical on all platforms
- The FFmpeg filter parameters (`loudnorm=I=-11:TP=-1.5:LRA=7`,
  `volumedetect`, `aresample=resampler=soxr:precision=28:cheby=1`) are
  calibrated to a specific output standard
- Any platform-specific modification would create divergence between
  Windows/Linux/macOS output
- These functions contain no platform-specific code and do not need any

**Enforcement:**
- Mark these functions with a `/* IMMUTABLE */` comment in the source
- Add a note in code review guidelines
- Validate in CI that these functions' content is unchanged across branches

### 9.4 Mandatory Audio Filter Dependencies

**Rule (MANDATORY):** The following FFmpeg filters are **required on every
platform** with **no fallback** (per `DEPENDENCIES_ANALYSIS.md`):

| Filter | FFmpeg Flag | Used In |
|--------|-------------|---------|
| `aresample` with `resampler=soxr` | `--enable-libsoxr` | All audio norm modes |
| `volumedetect` | (built-in) | `peak_two_pass()` |
| `loudnorm` | (built-in) | `loudnorm_two_pass()` |
| `volume` | (built-in) | `build_audio_filter_expr()` |
| `asplit` | (built-in) | Dual-audio mode in `build_ffmpeg_cmd()` |

**The only permitted fallback** is in the AAC codec selection chain:

```
aac_at (macOS) → libfdk_aac → native aac
```

`libfdk_aac` **may** fall back to native `aac`. No other filter or codec
may have a fallback. If `libsoxr` is not available, `platform_validate_audio_filters()`
must return 0 and `converter_set_options()` must return
`ERR_AUDIO_FILTER_VALIDATION_FAILED`.

### 9.5 Platform-Agnostic Calling Conventions

All `platform_*()` functions follow these conventions:

- Functions returning `const char*` return a pointer to static or
  per-init storage — never to a stack buffer. Callers must not free the result.
- Functions returning `char*` return heap-allocated strings — callers must
  `free()` the result.
- Functions returning `int` return 0 for false/failure, 1 for true/success,
  unless documented otherwise.
- All functions are safe to call after `platform_init()` returns 0.
- `platform_get_ffmpeg_bin()` and `platform_get_ffprobe_bin()` never return
  `NULL`. They return `""` (empty string) if the binary is not found, and
  callers must check for empty string before using the result.

---

## 10. Implementation Phases

### Phase 1: Extract Common Utilities (converter_common.c)

**Goal:** Create `converter_common.c` and `converter_common.h` with pure
utility functions that do not depend on platform code.

**Steps:**
1. Create `src/converter/converter_common.h`
2. Create `src/converter/converter_common.c`
3. Move `parse_time_hms()` from `converter.c` to `converter_common.c`
4. Add `get_cpu_count()` stub (calls `platform_get_cpu_count()`)
5. Add `get_filter_threads()` (unchanged logic)
6. Add `is_path_absolute()` (new utility)
7. Update `converter.c` to include `converter_common.h` and remove moved functions
8. Update `CMakeLists.txt` to add `converter_common.c`
9. Build and test — must compile and pass existing tests on Linux and macOS

**Risk:** Low. No behavior changes. Pure refactoring of helper functions.

---

### Phase 2: Define converter_platform.h Interface

**Goal:** Create the platform abstraction header with all function declarations.

**Steps:**
1. Create `src/converter/converter_platform.h` with the full interface (Section 6)
2. Include `converter_platform.h` in `converter.c`
3. Do NOT yet replace any existing calls — just verify the header compiles
4. Add stub implementations for each function that just call the existing
   code (to keep Linux/macOS builds working)

**Risk:** Low. Header-only step with no logic changes.

---

### Phase 3: Implement Platform-Specific Files

**Goal:** Create `platform/converter_linux.c`, `platform/converter_macos.c`,
and `platform/converter_windows.c` with full implementations of all
`platform_*()` functions.

**Steps:**
1. Create `src/converter/platform/` directory
2. Implement `platform/converter_linux.c`:
   - Move `get_exe_dir()` (Linux branch) → `linux_get_exe_dir()`
   - Move `is_executable()` (Linux) → `linux_is_executable()`
   - Wrap `linux_get_preferred_ffmpeg_bin()` in `linux_get_ffmpeg_bin()`
   - Implement all path operations using POSIX functions
   - Implement `linux_get_cpu_count()` using `sysconf()`
3. Implement `platform/converter_macos.c`:
   - Move `get_exe_dir()` (macOS branch) → `macos_get_exe_dir()`
   - Move `get_video_info()` → `macos_get_video_info()`
   - Move `calc_hevc_vt_bitrate_kbps()` → keep as static in macos file
   - Implement macOS binary search (MacPorts paths)
   - Implement `macos_get_cpu_count()` using `sysctlbyname()`
4. Implement `platform/converter_windows.c`:
   - `GetModuleFileNameW()` for exe dir
   - PATH search with `.exe` extension
   - `_mkdir()` for directory creation
   - `USERPROFILE` for home directory
   - CPU count via `GetSystemInfo()`
   - NVENC/QSV detection via `ffmpeg -encoders`
5. Add each platform file to `CMakeLists.txt`
6. Build each platform separately and verify

**Risk:** MEDIUM. New code, especially on Windows. Requires MSYS2 build
environment for Windows testing.

---

### Phase 4: Refactor converter.c to Use Platform Layer

**Goal:** Remove all platform `#ifdef` blocks from `converter.c` and replace
all platform-specific calls with `platform_*()` calls.

**Steps (in order):**
1. Remove `#include <unistd.h>` — replace usages with `platform_*()` calls
2. Remove `#include <libgen.h>` — no longer needed (replaced by `platform_get_filename()`)
3. Remove `#include "linux/runtime_probe.h"` — moved to `converter_linux.c`
4. Remove `#if defined(__APPLE__)` includes — moved to `converter_macos.c`
5. Remove `is_executable()` — moved to platform files
6. Remove `get_exe_dir()` — moved to platform files
7. Remove `resolve_bundled_bin()` — moved to platform files
8. Replace `get_ffmpeg_bin()` body with `return platform_get_ffmpeg_bin()`
9. Replace `get_ffprobe_bin()` body with `return platform_get_ffprobe_bin()`
10. Replace `get_cpu_count()` with call to `platform_get_cpu_count()`
11. Delete `format_eta()` (dead code)
12. Replace `mkdir_p()` with `platform_mkdir_recursive()`
13. Update `ensure_output_dir_writable()` to use `platform_get_home_dir()`,
    `platform_mkdir_recursive()`
14. Update `check_file()` to use `platform_get_null_device()` if needed
15. Remove `#if defined(__APPLE__)` block from `get_video_info()` / `calc_hevc_vt_bitrate_kbps()`
16. Update `converter_create()` to call `platform_init()`
17. Update `converter_destroy()` to call `platform_cleanup()`
18. Update `converter_set_options()` to call `platform_detect_gpu_support()`
    and `platform_validate_audio_filters()`
19. Update `make_output_name()` to use `platform_join_paths()` and
    `platform_get_filename()`
20. Update `build_ffmpeg_cmd()` to call `platform_get_video_codec_flags()`
21. Update `run_ffmpeg_encode_with_progress()` to call
    `platform_normalize_output_line()`
22. Update `converter_process_files()` to remove `#if defined(__APPLE__)` block
23. Add new error codes to `converter_error_string()`
24. Build and test on Linux — must pass all existing tests

**Risk:** HIGH. Core logic changes. Must be done carefully and tested after
each step.

---

### Phase 5: Test Each Platform

**Goal:** Verify that refactored code produces identical results to the
original code on all three platforms.

**Test checklist:**

**Linux:**
- [ ] Compiles without warnings
- [ ] `converter_create()` / `converter_destroy()` work
- [ ] `converter_set_options()` with `h264_vaapi` detects VAAPI correctly
- [ ] `peak_two_pass()` produces same gain as before refactoring
- [ ] `loudnorm_two_pass()` produces same JSON extraction as before
- [ ] `build_ffmpeg_cmd()` for each codec produces identical command string
- [ ] FFmpeg command executes and produces output file
- [ ] `make_output_name()` produces correct paths

**macOS:**
- [ ] Compiles without warnings
- [ ] Binary resolution finds ffmpeg at MacPorts or bundle paths
- [ ] `get_video_info()` / `macos_get_video_info()` produces same W/H/FPS
- [ ] `hevc_videotoolbox` bitrate calculation is unchanged
- [ ] All audio norm modes produce identical output

**Windows (MSYS2 MinGW):**
- [ ] Compiles without warnings under MSYS2 MinGW x64
- [ ] `platform_get_ffmpeg_bin()` finds `ffmpeg.exe` in PATH
- [ ] `platform_mkdir_recursive()` works with `C:\Users\...` paths
- [ ] `platform_get_home_dir()` returns `USERPROFILE` value
- [ ] `converter_process_files()` successfully converts a test file
- [ ] Audio filter validation passes (libsoxr, loudnorm available in MSYS2 ffmpeg)
- [ ] Progress reporting works (no `\r` parsing issues)

---

## Appendix: Platform Code Map

### A.1 Full Cross-Reference: Current → New Location

| Current Location | Current Function | New Location | New Name |
|-----------------|------------------|--------------|----------|
| converter.c:29 | `codec_is_linux_vaapi()` | converter.c | `codec_is_vaapi()` (renamed) |
| converter.c:35 | `codec_uses_mov_container()` | converter.c | unchanged |
| converter.c:42 | `audio_output_mode_is()` | converter.c | unchanged |
| converter.c:46 | `audio_output_mode_valid()` | converter.c | unchanged (add null guard) |
| converter.c:55 | `build_audio_filter_expr()` | converter.c | **IMMUTABLE** — unchanged |
| converter.c:97 | `codec_uses_aac_audio()` | converter.c | unchanged |
| converter.c:104 | `ffmpeg_encoder_available()` | converter.c | refactored |
| converter.c:154 | `is_executable()` | platform/*.c | `platform_is_executable()` |
| converter.c:158 | `get_exe_dir()` | platform/*.c | (internal to platform file) |
| converter.c:187 | `resolve_bundled_bin()` | platform/*.c | (internal to platform file) |
| converter.c:203 | `get_ffmpeg_bin()` | converter.c | wrapper → `platform_get_ffmpeg_bin()` |
| converter.c:220 | `get_ffprobe_bin()` | converter.c | wrapper → `platform_get_ffprobe_bin()` |
| converter.c:240 | `get_cpu_count()` | converter_common.c | `get_cpu_count()` |
| converter.c:257 | `get_filter_threads()` | converter_common.c | `get_filter_threads()` |
| converter.c:267 | `converter_create()` | converter.c | + `platform_init()` call |
| converter.c:272 | `converter_destroy()` | converter.c | + `platform_cleanup()` call |
| converter.c:280 | `converter_set_callbacks()` | converter.c | unchanged |
| converter.c:294 | `converter_set_options()` | converter.c | + `platform_detect_gpu()` call |
| converter.c:339 | `converter_stop()` | converter.c | unchanged |
| converter.c:347 | `converter_error_string()` | converter.c | + new error codes |
| converter.c:369 | `parse_time_hms()` | converter_common.c | `parse_time_hms()` |
| converter.c:378 | `format_eta()` | DELETE | dead code |
| converter.c:393 | `mkdir_p()` | platform/*.c | `platform_mkdir_recursive()` |
| converter.c:424 | `ensure_output_dir_writable()` | converter.c | refactored |
| converter.c:469 | `get_duration()` | converter.c | refactored (use platform_get_null_device) |
| converter.c:496 | `get_video_info()` | platform/converter_macos.c | `macos_get_video_info()` |
| converter.c:536 | `calc_hevc_vt_bitrate_kbps()` | platform/converter_macos.c | static helper |
| converter.c:558 | `check_file()` | converter.c | unchanged |
| converter.c:585 | `make_output_name()` | converter.c | refactored |
| converter.c:677 | `converter_make_output_name()` | converter.c | unchanged |
| converter.c:688 | `check_output_exists()` | converter.c | unchanged |
| converter.c:708 | `peak_two_pass()` | converter.c | **IMMUTABLE** — unchanged |
| converter.c:789 | `json_number_or_string_value()` | converter.c | unchanged |
| converter.c:796 | `loudnorm_two_pass()` | converter.c | **IMMUTABLE** — unchanged |
| converter.c:909 | `build_ffmpeg_cmd()` | converter.c | refactored |
| converter.c:1130 | `run_ffmpeg_encode_with_progress()` | converter.c | refactored |
| converter.c:1212 | `converter_process_files()` | converter.c | refactored |

---

*This document was generated based on analysis in `docs/CONVERTER_CODE_ANALYSIS.md`
and `docs/DEPENDENCIES_ANALYSIS.md`.*
