# CONVERTER_CODE_ANALYSIS.md

Detailed function-by-function analysis of `src/converter/converter.c` and
`src/converter/converter.h` for platform-portability review and refactoring
preparation.

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [converter.h Analysis](#2-converterh-analysis)
3. [converter.c Code Analysis by Section](#3-converterc-code-analysis-by-section)
   - 3.1 [Includes and Struct Definition](#31-includes-and-struct-definition)
   - 3.2 [Inline Helper Predicates](#32-inline-helper-predicates)
   - 3.3 [Audio Filter Building](#33-audio-filter-building)
   - 3.4 [System Detection & Binary Resolution](#34-system-detection--binary-resolution)
   - 3.5 [CPU Thread Count](#35-cpu-thread-count)
   - 3.6 [Create / Destroy / Callbacks / Options / Stop](#36-create--destroy--callbacks--options--stop)
   - 3.7 [Time Parsing & ETA Formatting](#37-time-parsing--eta-formatting)
   - 3.8 [Output Directory Preflight (mkdir_p)](#38-output-directory-preflight-mkdir_p)
   - 3.9 [ffprobe Duration Probe](#39-ffprobe-duration-probe)
   - 3.10 [macOS VideoToolbox Helpers](#310-macos-videotoolbox-helpers)
   - 3.11 [File Existence Checks](#311-file-existence-checks)
   - 3.12 [Output Name Generation](#312-output-name-generation)
   - 3.13 [Peak 2-Pass Analysis](#313-peak-2-pass-analysis)
   - 3.14 [Loudnorm 2-Pass Analysis](#314-loudnorm-2-pass-analysis)
   - 3.15 [FFmpeg Command Building](#315-ffmpeg-command-building)
   - 3.16 [FFmpeg Execution & Progress Parsing](#316-ffmpeg-execution--progress-parsing)
   - 3.17 [Main Processing Loop](#317-main-processing-loop)
4. [Platform-Independent Code Analysis](#4-platform-independent-code-analysis)
5. [Platform-Specific Code Analysis](#5-platform-specific-code-analysis)
6. [Windows-Specific Code Issues](#6-windows-specific-code-issues)
7. [Data Structure Needs](#7-data-structure-needs)
8. [Refactoring Opportunities](#8-refactoring-opportunities)
9. [Summary of Issues by Severity](#9-summary-of-issues-by-severity)
10. [Recommendations for Refactoring](#10-recommendations-for-refactoring)

---

## 1. Executive Summary

`converter.c` is a ~1,381-line, single-file C module that provides a complete
video/audio conversion pipeline on top of FFmpeg. It is currently the only
source file in the `src/converter/` directory and implements both platform-
agnostic conversion logic and platform-specific binary resolution, GPU
acceleration setup, and subprocess invocation in one monolithic file.

The module compiles successfully on **Linux** and **macOS**. It will **not**
compile on Windows as-is due to hard POSIX dependencies (`<unistd.h>`,
`readlink`, POSIX `mkdir`, `2>/dev/null` shell redirects, etc.).

### Key Findings

| Category | Count |
|---|---|
| CRITICAL compilation blockers on Windows | 6 |
| HIGH runtime failures on Windows | 9 |
| MEDIUM portability issues | 7 |
| Functions with platform-specific code | 7 |
| Immutable audio-processing functions (must not change) | 3 |

### Immutable Audio Algorithms Rule

The following three functions contain the audio processing algorithms that
**must remain identical on every platform**. They contain no platform-specific
code and must never be split by platform:

| Function | Lines | Algorithm |
|---|---|---|
| `build_audio_filter_expr()` | 55–95 | Audio filter expression builder |
| `peak_two_pass()` | 708–784 | Peak loudness 2-pass analysis |
| `loudnorm_two_pass()` | 796–904 | EBU R128 loudness 2-pass analysis |

### Mandatory Audio Filter Dependencies Rule

The following FFmpeg filters are **required on every platform** with **no
fallback**:

- `aresample` with `resampler=soxr` (`--enable-libsoxr`)
- `volumedetect`
- `loudnorm`
- `volume`
- `asplit`

The **only** permitted fallback is in the AAC codec selection chain:
`libfdk_aac → native aac`. No other fallback is allowed for audio filters.

---

## 2. converter.h Analysis

**File:** `src/converter/converter.h`  
**Lines:** 1–171

### 2.1 `ConverterError` Enum (lines 13–40)

```c
typedef enum {
    ERR_OK = 0,
    ERR_INPUT_NOT_FOUND,
    ERR_INPUT_NOT_REGULAR,
    ERR_INPUT_NOT_READABLE,
    ERR_OUTPUT_EXISTS,
    ERR_SKIP_FILE,
    ERR_PEAK_ANALYSIS_FAILED,
    ERR_LOUDNORM_ANALYSIS_FAILED,
    ERR_FFMPEG_FAILED,
    ERR_FFPROBE_FAILED,
    ERR_POPEN_FAILED,
    ERR_PCLOSE_FAILED,
    ERR_INVALID_OPTIONS,
    ERR_UNKNOWN
} ConverterError;
```

**Assessment:** Well-structured. All error codes are platform-agnostic.

**Issue — MEDIUM:** `ERR_POPEN_FAILED` and `ERR_PCLOSE_FAILED` expose the
`popen/pclose` implementation detail as part of the public API. On a future
Windows implementation that uses `CreateProcess` instead of `popen`, these
names become misleading. Consider renaming to `ERR_SUBPROCESS_START_FAILED`
and `ERR_SUBPROCESS_CLOSE_FAILED`.

### 2.2 `ConvertOptions` Struct (lines 45–83)

```c
typedef struct {
    char codec[32];
    int  profile;
    int  deblock;
    char audio_norm[32];
    char audio_output_mode[32];
    int genre;
    double gain;
    double I_target, TP_target, LRA_target;
    double measured_I, measured_TP, measured_LRA;
    double measured_thresh, measured_offset;
    int  overwrite;
    char output_dir[1024];
    int output_dir_status;
    char video_track_path[1024];
    char hw_device[1024];
    int hwaccel_enabled;
    int video_quality;
    int use_aac_for_h265;
    int hevc_vt_bitrate_kbps;
} ConvertOptions;
```

**Issues:**

- **MEDIUM:** `hevc_vt_bitrate_kbps` (line 81) is a macOS-specific field
  that is calculated at runtime. It leaks a macOS implementation detail into
  the shared header used by all platforms. Consider moving it to an opaque
  platform extension or calculating it inside `build_ffmpeg_cmd()` without
  storing in options.

- **MEDIUM:** `hw_device[1024]` (line 77) is populated on Linux with the
  VAAPI render node path (`/dev/dri/renderD128`). On Windows it would need to
  hold a DirectX device index or NVENC device string. Its semantics are
  currently undocumented and platform-specific.

- **MEDIUM:** `output_dir_status` (line 75) is declared but never set in the
  public API (only `output_dir` is used). Its purpose is unclear.

- **LOW:** Fixed-size char arrays (`codec[32]`, `audio_norm[32]`,
  `audio_output_mode[32]`) are adequate for current values but could silently
  truncate if longer codec names are added.

- **Missing field:** No field to store detected tool paths (ffmpeg, ffprobe,
  mkvmerge, MP4Box). On Linux, `LinuxCodecSupport` carries this, but there is
  no equivalent in `ConvertOptions` for other platforms.

### 2.3 `ConverterCallbacks` Struct (lines 88–127)

```c
typedef struct {
    void (*on_file_begin)(const char* filename, int index, int total);
    void (*on_file_end)(const char* filename, ConverterError status);
    void (*on_stage)(const char* stage_name);
    void (*on_progress_encode)(float percent, float fps, float eta_seconds);
    void (*on_progress_analysis)(float percent, float eta_seconds);
    void (*on_message)(const char* text);
    void (*on_error)(const char* text, ConverterError code);
    void (*on_complete)(void);
} ConverterCallbacks;
```

**Assessment:** Clean callback design. All callbacks are nullable (NULL-safe
checked at call sites).

**Issue — LOW:** No `on_warning` callback. Windows-specific non-fatal issues
(e.g., libfdk_aac fallback to native aac) currently report via `on_message`.
A dedicated `on_warning` would separate informational and diagnostic messages.

### 2.4 Public API (lines 137–165)

```c
Converter* converter_create(void);
void converter_destroy(Converter* c);
void converter_set_callbacks(Converter* c, const ConverterCallbacks* cb);
ConverterError converter_set_options(Converter* c, const ConvertOptions* opts);
ConverterError converter_process_files(Converter* c, const char** files, int file_count);
void converter_make_output_name(const char* input, const ConvertOptions* opts,
                                char* out, size_t out_sz);
void converter_stop(Converter* c);
const char* converter_error_string(ConverterError err);
```

**Assessment:** Minimal, stable API. No platform-specific symbols leak
through.

**Issue — MEDIUM:** `converter_make_output_name()` is a public function that
calls the private `make_output_name()`. The output path uses `/` as separator
(hardcoded in the private implementation). On Windows, callers may receive
paths with `/` separators mixed with `\` from the caller's own paths.

---

## 3. converter.c Code Analysis by Section

### 3.1 Includes and Struct Definition

**Lines 1–27**

```c
#include "converter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <jansson.h>
#include <sys/stat.h>
#include <unistd.h>       // ← CRITICAL: not available on Windows
#include <time.h>
#include <errno.h>
#include <libgen.h>       // ← CRITICAL: not available on Windows (dirname)
#if defined(__linux__)
#include "linux/runtime_probe.h"
#endif
#if defined(__APPLE__)
#include <math.h>
#include <sys/sysctl.h>
#include <mach-o/dyld.h>
#endif

struct Converter {
    ConvertOptions opts;
    ConverterCallbacks cb;
    int stop_flag;
};
```

**Issues:**

- **CRITICAL:** `#include <unistd.h>` is unconditional. On Windows/MSVC or
  MinGW without Cygwin compatibility, this header does not exist. It provides:
  `access()`, `readlink()`, `ssize_t`, `pclose()`, `popen()`, `sysconf()`.
  Every one of these is used in this file.

- **CRITICAL:** `#include <libgen.h>` is unconditional. On Windows, this
  header does not exist. It provides `dirname()`, which is used in
  `get_exe_dir()` on both the macOS and Linux branches.

- **MEDIUM:** The `Converter` struct is defined in the `.c` file (opaque
  pointer pattern). This is good for encapsulation, but the `stop_flag` field
  is set by `converter_stop()` without atomic operations. In a multi-threaded
  context, a separate thread calling `converter_stop()` while the processing
  loop reads `stop_flag` is a data race.

### 3.2 Inline Helper Predicates

**Lines 29–53**

```c
static int codec_is_linux_vaapi(const char* codec) { ... }   // line 29
static int codec_uses_mov_container(const char* codec) { ... } // line 35
static int audio_output_mode_is(...) { ... }                  // line 42
static int audio_output_mode_valid(...) { ... }               // line 46
```

**Assessment:** Simple, pure string comparisons. All are platform-agnostic.

**Issue — MEDIUM:** `codec_is_linux_vaapi()` (line 29) is named with the
platform in the function name, but it is called unconditionally in
`build_ffmpeg_cmd()` (line 918) which runs on all platforms. On non-Linux
builds, the VAAPI codec names will never be selected (they are not in the
codec list), so this is not a runtime bug, but it creates conceptual noise
and the Linux-specific naming is misleading.

**Issue — LOW:** `audio_output_mode_valid()` (lines 46–53) has an asymmetry:
`mode[0] == '\0'` is checked without a null pointer guard, while
`audio_output_mode_is()` accepts null. If `mode` is `NULL`, this is
undefined behavior.

```c
// Current code (line 47) — potential NULL dereference:
static int audio_output_mode_valid(const char* mode) {
    return mode[0] == '\0' || ...
```

### 3.3 Audio Filter Building

**Lines 55–95 — `build_audio_filter_expr()`**

```c
static void build_audio_filter_expr(const ConvertOptions* opts,
                                    char* filter, size_t filter_sz)
```

**Purpose:** Constructs the FFmpeg `-af` filter graph string based on the
selected normalization mode.

**Platform-specific code:** None.

**Assessment:** This function is correct, complete, and platform-independent.
The filter expression for each mode is hardcoded as a string literal — no
dynamic lookup, no platform branches.

**RULE: This function is IMMUTABLE. It must not be modified or split by
platform under any circumstances.**

Filter expressions produced:

| `audio_norm` value | Filter string |
|---|---|
| `"none"` | `aresample=resampler=soxr:precision=28:cheby=1` |
| `"peak_norm"` | `...volume=-3dB` |
| `"peak_norm_2pass"` | `...volume=<gain>dB` |
| `"loudness_norm"` | `...loudnorm=I=-11:TP=-1.5:LRA=7` |
| `"loudness_norm_2pass"` | `...loudnorm=I=...:linear=true` (with all measured params) |
| else (fallback) | `aresample=resampler=soxr:precision=28:cheby=1` |

**Issue — LOW:** The fallback for an unknown `audio_norm` value is silent —
it produces `aresample` only without a warning. If an invalid value is passed,
no error is raised. Consider adding a `on_message` or `on_warning` callback.

### 3.4 System Detection & Binary Resolution

#### `codec_uses_aac_audio()` — lines 97–100

```c
static int codec_uses_aac_audio(const char* codec) {
    return codec && (strcmp(codec, "hevc_videotoolbox") == 0);
}
```

Only `hevc_videotoolbox` forces AAC audio. This is macOS-specific logic
embedded in a cross-platform function. On Windows/Linux this always returns 0
(no-op), but the function name does not indicate it is macOS-specific.

#### `ffmpeg_encoder_available()` — lines 104–152

```c
static int ffmpeg_encoder_available(const char* encoder_name) {
    static int initialized = 0;
    static int has_aac_at = 0, has_libfdk_aac = 0, has_aac = 0;
    ...
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -hide_banner -v error -encoders 2>/dev/null",   // ← issue
             ffmpeg_bin);
    FILE* fp = popen(cmd, "r");
    ...
```

**Issues:**

- **HIGH:** `2>/dev/null` in the command string (line 118) is a Unix shell
  redirect. On Windows MSYS2's `popen()` shell, this works because MSYS2
  uses a Unix-like shell. However, if the application is launched from a
  native Windows CMD or PowerShell context, `popen()` may use `cmd.exe` as
  the shell where `2>/dev/null` is not valid syntax. The correct Windows
  equivalent is `2>nul`.

- **HIGH:** Static state (`initialized`, `has_aac_at`, etc.) is not
  thread-safe. If `ffmpeg_encoder_available()` is called concurrently from
  multiple threads (e.g., two `converter_process_files()` calls in parallel),
  there is a data race.

- **HIGH:** `popen()` on Windows returns `\r\n` line endings. The strstr
  probes for `" aac_at"`, `" libfdk_aac"`, `" aac "` — these string matches
  will still succeed, but if the line buffer ends with `\r` before `\n`,
  `strstr(line, " aac ")` may fail if the space after `aac` is missing due
  to the `\r` stripping the trailing space. This is edge-case dependent.

- **MEDIUM:** `aac_at` will never be found on Windows — FFmpeg for Windows
  does not include the Apple AudioToolbox encoder. This is functionally
  correct (the fallback chain handles it), but the probe wastes a call for
  `aac_at`.

#### `is_executable()` — lines 154–156

```c
static int is_executable(const char* path) {
    return (access(path, X_OK) == 0);
}
```

**Issues:**

- **CRITICAL:** `access()` is a POSIX function from `<unistd.h>`, not
  available on Windows MSVC. MinGW provides a `_access()` equivalent, but
  `X_OK` (execute permission check) is meaningless on Windows — Windows
  determines executability by file extension (`.exe`, `.bat`, `.cmd`), not
  POSIX permission bits.

- **HIGH:** Even on MinGW with `_access()`, `X_OK` maps to `0` (exists check)
  rather than a true execute permission check. This means any existing file
  would be considered "executable", including DLL files, text files, etc.

  **Fix for Windows:** Check file extension (`.exe`) or use `PathFileExists()`.

#### `get_exe_dir()` — lines 158–185

```c
static const char* get_exe_dir(void) {
    static char exe_dir[1024] = {0};
    static int initialized = 0;
    if (initialized) return exe_dir;

#if defined(__APPLE__)
    char exe_path[1024];
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) == 0) {
        char resolved[1024];
        if (realpath(exe_path, resolved)) {
            strncpy(exe_dir, dirname(resolved), sizeof(exe_dir) - 1);
        }
    }
#else
    char exe_path[1024];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);  // ← CRITICAL
    if (len != -1) {
        exe_path[len] = '\0';
        strncpy(exe_dir, dirname(exe_path), sizeof(exe_dir) - 1);
    }
#endif
    initialized = 1;
    return exe_dir;
}
```

**Issues:**

- **CRITICAL:** The `#else` branch (everything that is not macOS) uses
  `readlink("/proc/self/exe", ...)`. This is Linux-specific. On Windows,
  `/proc/self/exe` does not exist. `readlink` is not available on Windows
  (it is declared in `<unistd.h>`). This branch will fail to compile or,
  at runtime, will always return an empty `exe_dir`.

  **Fix for Windows:** Use `GetModuleFileNameW(NULL, ...)` followed by
  `PathRemoveFileSpecW()` or equivalent.

- **MEDIUM:** `dirname()` from `<libgen.h>` is used in both the macOS and
  the `#else` branch. On Windows, `<libgen.h>` does not exist. The
  `dirname()` behavior (may modify its input, returns pointer into input or
  static storage) is POSIX-specific.

- **MEDIUM:** `static char exe_dir[1024]` — not thread-safe. A race on
  `initialized` flag.

#### `resolve_bundled_bin()` — lines 187–201

```c
static const char* resolve_bundled_bin(const char* name) {
    const char* exe_dir = get_exe_dir();
    if (exe_dir[0] == '\0') return NULL;

    static char path[1024];                    // ← not thread-safe
    snprintf(path, sizeof(path), "%s/%s", exe_dir, name);   // ← always '/'
    if (is_executable(path)) return path;

#if defined(__APPLE__)
    snprintf(path, sizeof(path), "%s/../Resources/bin/%s", exe_dir, name);
    if (is_executable(path)) return path;
#endif

    return NULL;
}
```

**Issues:**

- **HIGH:** Path separator is hardcoded as `/` (line 192). On Windows, while
  `\` is the canonical separator, Windows APIs generally accept `/` in paths.
  However, some Windows tools and shell operations may be confused by mixed
  separators.

- **HIGH:** `static char path[1024]` is not thread-safe. Two concurrent calls
  will corrupt each other's result.

- **HIGH:** On Windows, `exe_dir` will always be empty (because `get_exe_dir()`
  fails on Windows — see above), so `resolve_bundled_bin()` will always return
  `NULL`. Any binary bundled next to the executable will not be found.

#### `get_ffmpeg_bin()` — lines 203–218

```c
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
```

**Issues:**

- **CRITICAL:** On Windows, neither the `__linux__` branch nor the macOS
  branch is taken. `resolve_bundled_bin("ffmpeg")` returns `NULL` (because
  `get_exe_dir()` returns empty string). The function returns `""` (empty
  string). Every subsequent `popen()` invocation will try to run a command
  starting with `""` — which will fail with a shell error.

- **HIGH:** There is no Windows-specific binary search logic. On Windows,
  `ffmpeg` may be:
  1. Installed in MSYS2 at `/mingw64/bin/ffmpeg.exe`
  2. Installed via a standalone package somewhere on `PATH`
  3. Bundled next to the `.exe`
  4. Installed via chocolatey in a system path

  None of these are probed. The correct approach is to search `PATH` for
  `ffmpeg.exe` on Windows.

- **MEDIUM:** The macOS resolution order differs from `DEPENDENCIES_ANALYSIS.md`
  which documents `/opt/local/bin/ffmpeg8` → `/opt/local/bin/ffmpeg` as MacPorts
  priority. The actual code only tries bundled binary after env vars, not
  MacPorts paths.

#### `get_ffprobe_bin()` — lines 220–235

Identical structure to `get_ffmpeg_bin()`. Inherits all the same issues.

### 3.5 CPU Thread Count

**Lines 240–262 — `get_cpu_count()` and `get_filter_threads()`**

```c
static int get_cpu_count(void) {
#if defined(__APPLE__)
    int count = 0;
    size_t size = sizeof(count);
    if (sysctlbyname("hw.ncpu", &count, &size, NULL, 0) == 0 && count > 0)
        return count;
    long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cpus < 1) return 1;
    return (int)num_cpus;
#else
    long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);   // ← HIGH: not on Windows
    if (num_cpus < 1) return 1;
    return (int)num_cpus;
#endif
}

int get_filter_threads(void) {
    int cpus = get_cpu_count();
    int threads = cpus / 2;
    if (threads < 1) threads = 1;
    return threads;
}
```

**Issues:**

- **HIGH:** The `#else` branch (selected for Windows) uses `sysconf()`, which
  is declared in `<unistd.h>` and is not available on Windows. MinGW does
  not provide `sysconf()`.

  **Fix for Windows:** Use `GetSystemInfo()` which fills a `SYSTEM_INFO`
  struct with `dwNumberOfProcessors`, or read the `NUMBER_OF_PROCESSORS`
  environment variable as a fallback.

- **MEDIUM:** `get_filter_threads()` is declared without `static`, making it
  linkable by external translation units. This is likely unintentional — it
  should be `static`.

### 3.6 Create / Destroy / Callbacks / Options / Stop

#### `converter_create()` — lines 267–270

```c
Converter* converter_create(void) {
    Converter* c = calloc(1, sizeof(Converter));
    return c;
}
```

**Assessment:** Correct, platform-agnostic.

**Missing:** No initialization of platform-specific state (binary paths,
GPU device enumeration). On Linux, `linux_probe_codec_support()` is called
lazily inside `converter_set_options()`. A `converter_init()` phase would
make platform setup explicit and allow early error reporting.

#### `converter_destroy()` — lines 272–275

```c
void converter_destroy(Converter* c) {
    if (!c) return;
    free(c);
}
```

**Assessment:** Correct.

#### `converter_set_callbacks()` — lines 280–289

**Assessment:** Correct, platform-agnostic.

#### `converter_set_options()` — lines 294–334

```c
ConverterError converter_set_options(Converter* c, const ConvertOptions* opts) {
#if defined(__linux__)
    LinuxCodecSupport support;
#endif
    ...
    c->opts = *opts;

    if (!audio_output_mode_valid(c->opts.audio_output_mode))
        return ERR_INVALID_OPTIONS;

#if defined(__linux__)
    if (codec_is_linux_vaapi(c->opts.codec)) {
        linux_probe_codec_support(&support);
        ...
        c->opts.hwaccel_enabled = 1;
    }
#endif
    return ERR_OK;
}
```

**Issues:**

- **MEDIUM:** The Linux VAAPI probe happens inside `converter_set_options()`,
  which means it runs every time options are changed. The probe itself is
  cached inside `runtime_probe.c`, but the structure is still awkward.

- **MEDIUM:** No validation of codec names against platform capability on
  macOS or Windows. If a caller passes `"hevc_videotoolbox"` on Linux or
  Windows, the code silently proceeds to encode with that codec string, which
  FFmpeg will reject at runtime.

- **MEDIUM:** `audio_output_mode_valid()` is called without a null-pointer
  guard on `c->opts.audio_output_mode[0]` — if `opts->audio_output_mode` is
  uninitialized binary garbage (not `'\0'`-terminated), this is undefined
  behavior.

#### `converter_stop()` — lines 339–342

```c
void converter_stop(Converter* c) {
    if (!c) return;
    c->stop_flag = 1;
}
```

**Issue — MEDIUM:** `stop_flag` is a plain `int`. Setting it from one thread
while the processing loop reads it in another thread is a data race (C11
requires `_Atomic` or synchronization primitives for multi-thread access to
shared state).

### 3.7 Time Parsing & ETA Formatting

**Lines 369–388 — `parse_time_hms()` and `format_eta()`**

```c
static double parse_time_hms(const char *s) {
    int h = 0, m = 0;
    double sec = 0.0;
    if (sscanf(s, "%d:%d:%lf", &h, &m, &sec) == 3)
        return h * 3600.0 + m * 60.0 + sec;
    return 0.0;
}
```

**Assessment:** Platform-agnostic. `sscanf` with `%lf` is standard C99.

**Issue — HIGH (Windows):** FFmpeg on Windows (when using MSYS2 popen) will
produce progress output with `\r\n` line endings instead of `\n`. The string
passed to `parse_time_hms()` may contain a trailing `\r` character before
the newline, which won't affect `sscanf` (it stops at non-matching chars),
but the `time=` string search pattern:

```c
char* tpos = strstr(line, "time=");
```

... will still find the substring, and `parse_time_hms(tpos + 5)` will parse
correctly. However, `strncmp(line, "out_time_ms=", 12)` in
`run_ffmpeg_encode_with_progress()` will also work because it only checks the
prefix.

The `\r\n` issue is therefore not a parsing bug here, but is worth noting
for future regex-style matching.

**Note:** `format_eta()` in `converter.c` is declared but never called by any
function within that file. It is dead code within `converter.c`. Similar
`format_eta()` functions exist in the platform-specific progress files
(`src/platform/linux/progress.c`, `src/platform/macos/progress.c`,
`src/platform/windows/progress.c`) where they are actively used. The version
in `converter.c` is a duplicate that was not removed when progress reporting
was moved to platform files, and should be deleted.

### 3.8 Output Directory Preflight (mkdir_p)

**Lines 393–464**

#### `mkdir_p()` — lines 393–422

```c
static int mkdir_p(const char* path) {
    char tmp[1024];
    ...
    strcpy(tmp, path);
    if (len > 1 && tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '/') {                        // ← HIGH: only '/' handled
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)  // ← CRITICAL: POSIX mkdir
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)          // ← CRITICAL: POSIX mkdir
        return -1;
    return 0;
}
```

**Issues:**

- **CRITICAL:** `mkdir(path, mode)` is a POSIX function with two arguments.
  On Windows, `mkdir` (from `<direct.h>`) takes only one argument — the
  mode parameter does not exist. Calling `mkdir(tmp, 0755)` will not compile
  on MSVC. On MinGW, `_mkdir()` is the correct function.

- **HIGH:** The path separator loop only handles `/`. On Windows, paths may
  use `\` (backslash). If `output_dir` contains backslashes, `mkdir_p` will
  never split the path correctly and will try to create the entire path as a
  single directory name. Example:
  ```
  C:\Users\User\output   → mkdir_p sees no '/' separators → tries to
                            create the entire string as one directory name
  ```

- **HIGH:** The directory length is hardcoded to `1024` characters. Windows
  paths can be up to `MAX_PATH` (260) by default, or up to 32767 with the
  long path prefix `\\?\`. Using `1024` truncates on very long Windows paths.

#### `ensure_output_dir_writable()` — lines 424–464

```c
static ConverterError ensure_output_dir_writable(
    Converter* c, const ConvertOptions* opts,
    char* out_dir, size_t out_dir_sz
) {
    const char* home = getenv("HOME");           // ← HIGH: not set on Windows
    if (!home || home[0] == '\0')
        home = ".";
    snprintf(out_dir, out_dir_sz, "%s/ffmpeg_converter", home);
    ...
    if (access(out_dir, W_OK) != 0) ...          // ← HIGH: W_OK on Windows
```

**Issues:**

- **HIGH:** `getenv("HOME")` returns `NULL` on Windows. Windows uses
  `USERPROFILE` or `HOMEDRIVE` + `HOMEPATH` environment variables. The
  fallback to `"."` (current directory) is a partial workaround but does not
  create a user-appropriate output location.

  **Fix:** On Windows: `getenv("USERPROFILE")` or
  `SHGetKnownFolderPath(FOLDERID_Documents, ...)`.

- **HIGH:** `access(out_dir, W_OK)` — `W_OK` is POSIX. On Windows MinGW,
  `_access()` can be used with `02` for write access, but the semantics
  differ: on Windows, write access to directories is controlled by ACLs, not
  POSIX permission bits. `_access()` on a directory does not reliably check
  write permission on Windows.

  **Fix:** On Windows, use `CreateFile()` with `GENERIC_WRITE` and check
  for success to properly test directory write permission.

- **HIGH:** The separator between `home` and `"ffmpeg_converter"` is `/`.
  While Windows accepts forward slashes in paths, mixed separators can
  confuse some tools and shell operations.

### 3.9 ffprobe Duration Probe

**Lines 469–488 — `get_duration()`**

```c
static double get_duration(const char *input) {
    char cmd[2048];
    const char *ffprobe_bin = get_ffprobe_bin();
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -v error -show_entries format=duration "
             "-of default=noprint_wrappers=1:nokey=1 \"%s\" 2>/dev/null",
             ffprobe_bin, input);

    FILE *fp = popen(cmd, "r");
    ...
    return atof(buf);
}
```

**Issues:**

- **HIGH:** `2>/dev/null` at the end of the command is a Unix shell redirect.
  In a Windows native CMD/PowerShell environment it is `2>nul`. In MSYS2
  `popen()` uses `/bin/sh` which handles `2>/dev/null`, so this works in MSYS2.

- **HIGH:** Input path `input` is double-quoted with `\"...\"`  in the command
  string. On Windows with paths containing spaces, the quoting works. However,
  if the path contains special characters like `&`, `|`, `(`, `)`, `>`, `<`,
  `^`, they are interpreted by `cmd.exe` as shell metacharacters even inside
  double quotes when using `popen()` via `cmd.exe`. Under MSYS2 `/bin/sh`,
  these are handled correctly by double-quoting.

- **MEDIUM:** `atof(buf)` has no error detection — if ffprobe returns nothing
  or an error message, `atof` returns `0.0`, which silently disables all
  progress reporting.

- **MEDIUM:** `pclose()` return value is not checked. If ffprobe fails (returns
  non-zero exit code), the duration is returned as whatever was parsed (possibly
  `0.0`) with no error indication.

### 3.10 macOS VideoToolbox Helpers

**Lines 493–553 — guarded by `#if defined(__APPLE__)`**

#### `get_video_info()` — lines 496–530

```c
#if defined(__APPLE__)
static void get_video_info(const char *input,
                           int *out_width, int *out_height, double *out_fps) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -v error -select_streams v:0"
             " -show_entries stream=width,height,r_frame_rate"
             " -of default=noprint_wrappers=1:nokey=1 \"%s\" 2>/dev/null",
             ffprobe_bin, input);
    ...
}
```

**Assessment:** Correctly guarded by `#if defined(__APPLE__)`. Platform-
specific and should remain so.

**Issue — MEDIUM:** Like `get_duration()`, uses `2>/dev/null` which is Unix-
specific. Since this function is macOS-only, this is not an immediate Windows
problem, but should be noted for cross-platform refactoring.

#### `calc_hevc_vt_bitrate_kbps()` — lines 536–551

```c
static int calc_hevc_vt_bitrate_kbps(int width, int height, double fps) {
    const double BASE_KBPS   = 35000.0;
    const double BASE_PIXELS = 3840.0 * 2160.0;
    const double BASE_FPS    = 24.0;
    double pixel_ratio = (double)(width * height) / BASE_PIXELS;
    double fps_ratio   = pow(fps / BASE_FPS, 0.75);
    double kbps        = BASE_KBPS * pixel_ratio * fps_ratio;
    if (kbps < 2000.0)  kbps = 2000.0;
    if (kbps > 80000.0) kbps = 80000.0;
    return (int)kbps;
}
```

**Assessment:** Pure math, correctly guarded by `#if defined(__APPLE__)`.
Uses `pow()` from `<math.h>`, which is platform-agnostic.

**Issue — LOW:** `width * height` may overflow for very large resolutions
(e.g., 8K: 7680×4320 = 33 177 600, well within `int` range on 32-bit). Not
a real-world issue.

### 3.11 File Existence Checks

**Lines 558–703**

#### `check_file()` — lines 558–580

```c
static ConverterError check_file(Converter* c, const char *file) {
    struct stat st;
    if (stat(file, &st) != 0) { ... }
    if (!S_ISREG(st.st_mode)) { ... }
    if (access(file, R_OK) != 0) { ... }
    return ERR_OK;
}
```

**Issues:**

- **MEDIUM:** `stat()` and `S_ISREG()` are POSIX but MinGW provides a
  compatible implementation. However, `S_ISREG()` may not be defined in
  MinGW's `<sys/stat.h>` — it may need `S_ISREG(m) ((m & S_IFMT) == S_IFREG)`.

- **MEDIUM:** `access(file, R_OK)` — on Windows, `R_OK` is not meaningful
  for directories in the same way as on POSIX. For files, MinGW's `_access()`
  accepts `04` (read permission), which does test for read access, but on NTFS
  the permission is determined by ACLs.

#### `make_output_name()` — lines 585–675

```c
static void make_output_name(const char* input, const ConvertOptions* opts,
                             char* out, size_t out_sz) {
    const char* slash = strrchr(input, '/');
#ifdef _WIN32
    const char* backslash = strrchr(input, '\\');
    if (backslash && (!slash || backslash > slash))
        slash = backslash;
#endif
    const char* name = slash ? slash + 1 : input;
    ...
    snprintf(out, out_sz, "%s/%s", opts->output_dir, filename);  // ← HIGH: '/' separator
}
```

**Issues:**

- **HIGH:** While the basename extraction (lines 595–600) correctly handles
  both `/` and `\` on Windows (there is a `#ifdef _WIN32` guard), the final
  path construction at line 663 always uses `/` as the separator:
  ```c
  snprintf(out, out_sz, "%s/%s", opts->output_dir, filename);
  ```
  If `output_dir` is a Windows-style path like `C:\Users\User\output`, the
  result will be `C:\Users\User\output/filename_converted.mkv` — mixing
  separators. This works in most cases on Windows, but is non-idiomatic and
  can break some tools.

- **LOW:** The truncation fallback (lines 634–648) truncates the filename
  but does not update the `base` variable — the `truncated` buffer is a
  copy of `base` with a `snprintf` into the same size buffer, so no actual
  truncation occurs (the `truncated` and `base` arrays have the same size
  `512`). This is dead code that does nothing different from the non-truncated
  path.

#### `converter_make_output_name()` — lines 677–684

Public wrapper for `make_output_name()`. No additional issues beyond those
in `make_output_name()`.

#### `check_output_exists()` — lines 688–703

```c
static ConverterError check_output_exists(Converter* c, const char* output) {
    struct stat st;
    if (stat(output, &st) == 0) {
        if (c->opts.overwrite == 0) {
            return ERR_OUTPUT_EXISTS;
        }
    }
    return ERR_OK;
}
```

**Assessment:** Uses `stat()` which is available on Windows via MinGW.
Platform-agnostic in practice.

### 3.12 Output Name Generation

*(Covered in Section 3.11 above.)*

### 3.13 Peak 2-Pass Analysis

**Lines 708–784 — `peak_two_pass()`**

```c
static ConverterError peak_two_pass(Converter* c, const char* input,
                                    double* out_gain) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "\"%s\" -filter_threads %d -vn -i \"%s\" -af volumedetect -f null - 2>&1",
        ffmpeg_bin, filter_threads, input);

    FILE* fp = popen(cmd, "r");
    ...
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "time=")) { ... parse progress ... }
        if (strstr(line, "max_volume:")) { ... parse max_volume ... }
    }
    int status = pclose(fp);
    ...
    *out_gain = target - maxv;
    return ERR_OK;
}
```

**RULE: This function's ALGORITHM is IMMUTABLE. The `volumedetect` filter
usage, the `max_volume` extraction, the gain calculation (`target - maxv`)
must not change on any platform.**

**Issues (implementation, not algorithm):**

- **HIGH:** `2>&1` redirects stderr to stdout — this is standard and works
  on both Windows MSYS2 and Unix. Not an issue here.

- **HIGH:** Input path `input` inserted literally into the command. If `input`
  contains characters like `(`, `)`, `&`, `|` (even inside double-quotes),
  cmd.exe shell (Windows native) may interpret them. MSYS2 sh handles this
  correctly.

- **MEDIUM:** If ffprobe returns non-zero (file not found, corrupt input),
  `status != 0` triggers the error. But if the analysis runs but produces no
  `max_volume:` line (e.g., silent audio), `maxv = 0.0` and `*out_gain = -3.0`.
  This is correct (target is -3 dBFS, silence has max_volume 0 dBFS, so gain
  is -3 dB), but silently proceeds.

- **LOW:** `start_ts` uses `(double)time(NULL)` for elapsed time. This has
  1-second granularity, making ETA estimates jerky for short files. On Windows,
  `time()` has the same 1-second resolution.

### 3.14 Loudnorm 2-Pass Analysis

**Lines 796–904 — `loudnorm_two_pass()`**

```c
static ConverterError loudnorm_two_pass(
    Converter* c, const char* input,
    double I_target, double TP_target, double LRA_target,
    double* I, double* TP, double* LRA, double* thresh, double* offset
) {
    snprintf(cmd, sizeof(cmd),
        "\"%s\" -filter_threads %d -vn -i \"%s\" -af "
        "\"loudnorm=I=%.1f:TP=%.1f:LRA=%.1f:linear=true:print_format=json\" "
        "-f null - 2>&1",
        ffmpeg_bin, filter_threads, input, I_target, TP_target, LRA_target);
    ...
    // collect all output into buf[131072]
    // find last '{' ... '}' (JSON block)
    // parse with jansson
    *I      = json_number_or_string_value(json_object_get(root, "input_i"));
    *TP     = json_number_or_string_value(json_object_get(root, "input_tp"));
    *LRA    = json_number_or_string_value(json_object_get(root, "input_lra"));
    *thresh = json_number_or_string_value(json_object_get(root, "input_thresh"));
    *offset = json_number_or_string_value(json_object_get(root, "target_offset"));
    json_decref(root);
    return ERR_OK;
}
```

**RULE: This function's ALGORITHM is IMMUTABLE. The `loudnorm` filter
parameters, JSON parsing of `input_i`, `input_tp`, `input_lra`,
`input_thresh`, `target_offset`, and their assignment to out-params must
not change on any platform.**

**Issues (implementation, not algorithm):**

- **HIGH:** `buf[131072]` is stack-allocated. For very long FFmpeg output
  (e.g., large files with many progress lines interleaved before the JSON),
  the buffer may overflow. The code truncates with:
  ```c
  if (pos + len < sizeof(buf) - 1) {
      memcpy(buf + pos, line, len);
      pos += len;
  }
  ```
  This silently drops lines that don't fit. If the JSON block itself is near
  the end of a very large output and gets partially dropped, JSON parsing fails.
  The `strrchr(buf, '{')` strategy is fragile for large outputs.

- **HIGH:** `strrchr(buf, '{')` finds the LAST `{` in the buffer, not the
  JSON block. If FFmpeg output contains embedded `{` characters in other lines
  (e.g., filter graph descriptions), this may select the wrong block. This
  is a robustness issue.

- **MEDIUM:** `json_number_or_string_value()` (lines 789–793) handles both
  numeric and string JSON values for the same field — FFmpeg loudnorm output
  sometimes returns `-inf` as a string. The helper correctly handles this,
  but `atof("-inf")` behavior is platform-dependent (returns `±HUGE_VAL` on
  POSIX, may return 0 or platform value on Windows MSVC).

  On MinGW, `atof("-inf")` returns the correct negative infinity.

- **MEDIUM:** The accumulation buffer approach mixes progress output lines
  with JSON output. A cleaner approach would be to detect the start `{` in
  the stream and collect only from that point.

### 3.15 FFmpeg Command Building

**Lines 909–1125 — `build_ffmpeg_cmd()`**

```c
static void build_ffmpeg_cmd(
    Converter* c, const char* input, const char* output,
    char* cmd_out, size_t cmd_out_sz
) {
    char cmd[16384];
    cmd[0] = 0;

    build_audio_filter_expr(opts, audio_filter, sizeof(audio_filter));

    snprintf(cmd, sizeof(cmd), "\"%s\" ", ffmpeg_bin);
    ...
    // append via strcat()
    strcat(cmd, "-i \"");
    strcat(cmd, input);
    strcat(cmd, "\" ");
    ...
    strncpy(cmd_out, cmd, cmd_out_sz);
    cmd_out[cmd_out_sz - 1] = 0;
}
```

**Issues:**

- **CRITICAL (Windows):** The entire command is built by concatenating strings
  into a `char cmd[16384]` buffer using `strcat()`. **There is no buffer
  overflow protection on `cmd`**. Each `strcat()` call blindly appends to the
  buffer. If the total command length exceeds 16384 bytes (including long paths
  or long filter expressions), the stack buffer overflows silently.

  This is a security vulnerability (stack buffer overflow) on all platforms,
  but more likely on Windows where paths tend to be longer (e.g.,
  `C:\Users\LongUserName\Videos\...`).

- **HIGH:** Input path and output path are inserted into the command string
  with only double-quote wrapping:
  ```c
  strcat(cmd, "-i \"");
  strcat(cmd, input);
  strcat(cmd, "\" ");
  ```
  On Windows, if `input` contains a double-quote character (legal in NTFS
  file names, e.g., `my "file".mov`), the command string becomes malformed:
  ```
  ffmpeg -i "my "file".mov"
  ```
  This will cause FFmpeg to misparse the input path.

  The same issue applies to the `output` path.

- **HIGH:** The VAAPI device path is inserted similarly:
  ```c
  strcat(cmd, "-vaapi_device ");
  strcat(cmd, "\"");
  strcat(cmd, opts->hw_device);  // no escaping
  strcat(cmd, "\" ");
  ```
  If `hw_device` contains special characters (unlikely for `/dev/dri/renderD128`
  but possible for future GPU path formats on other platforms), this breaks.

- **HIGH:** The audio filter expression `audio_filter` is inserted with:
  ```c
  strcat(cmd, "-filter_complex \"[0:a:0]");
  strcat(cmd, audio_filter);
  strcat(cmd, ",asplit=2[aout0][aout1]\" ");
  ```
  and also:
  ```c
  strcat(cmd, "-af \"");
  strcat(cmd, audio_filter);
  strcat(cmd, "\" ");
  ```
  The filter expression itself contains `:` characters (e.g.,
  `aresample=resampler=soxr:precision=28:cheby=1`). On Windows CMD shell,
  `:` inside double-quoted arguments is safe. Under MSYS2 sh, it is also safe.
  No issue here.

- **MEDIUM:** `build_ffmpeg_cmd()` does not validate that `cmd_out_sz` is
  large enough to hold the result. If `cmd_out_sz < 16384`, `strncpy` will
  silently truncate the command.

- **MEDIUM:** AAC encoder selection uses three separate booleans
  (`has_aac_at`, `has_libfdk_aac`, `has_native_aac`) but the selection logic
  is duplicated three times — for dual-audio, fdk-single, and h265-aac
  modes (lines 1050–1102). This is a refactoring opportunity.

- **MEDIUM:** `codec_is_linux_vaapi(opts->codec)` is called unconditionally
  on all platforms (line 918). On Windows/macOS, this always returns 0, so
  no incorrect behavior, but it is conceptually wrong.

### 3.16 FFmpeg Execution & Progress Parsing

**Lines 1130–1207 — `run_ffmpeg_encode_with_progress()`**

```c
static ConverterError run_ffmpeg_encode_with_progress(
    Converter* c, const char* cmd_base, double duration
) {
    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "%s 2>&1", cmd_base);   // ← may truncate

    FILE* fp = popen(cmd, "r");
    ...
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "out_time_ms=", 12) == 0) { ... }
        else if (strncmp(line, "fps=", 4) == 0) { ... }
        else if (strncmp(line, "progress=", 9) == 0) { ... }
        ...
    }
    int status = pclose(fp);
    if (status != 0) { return ERR_FFMPEG_FAILED; }
```

**Issues:**

- **HIGH:** `snprintf(cmd, sizeof(cmd), "%s 2>&1", cmd_base)` — `cmd` is only
  `8192` bytes, but `cmd_base` (built by `build_ffmpeg_cmd()`) can be up to
  `16384` bytes. The `snprintf` will silently truncate the command string,
  potentially cutting off the output path, which will cause FFmpeg to fail with
  a cryptic error.

- **HIGH:** `popen()` returns `\r\n` line endings on Windows (in native mode).
  The `strncmp(line, "out_time_ms=", 12)` and `strncmp(line, "fps=", 4)` checks
  look at the **beginning** of the line, so `\r\n` at the **end** does not
  affect these prefix checks. However, `atof(line + 12)` will parse the
  `out_time_ms` value and stop at the `\r`, returning the correct numeric
  value. **Not a bug**, but worth documenting.

- **MEDIUM:** `2>&1` is part of the command and will be present in the command
  string as-is. Under Windows CMD/PowerShell `popen()`, this works. Under
  MSYS2, this also works. So `2>&1` is portable in the MSYS2 context.

- **MEDIUM:** The `cmd[8192]` buffer is passed the result of appending
  `" 2>&1"` to `cmd_base`. If `cmd_base` is exactly 8187 chars, the appended
  string `" 2>&1"` is 5 chars, and the result fits (8192). But if `cmd_base`
  is longer (up to 16384), truncation occurs silently.

### 3.17 Main Processing Loop

**Lines 1212–1380 — `converter_process_files()`**

```c
ConverterError converter_process_files(
    Converter* c, const char** files, int file_count
) {
    ...
    for (int i = 0; i < file_count; i++) {
        ...
        // peak_norm_2pass
        // loudness_norm_2pass
        // build_ffmpeg_cmd
        // run_ffmpeg_encode_with_progress
    }
    if (c->cb.on_complete) c->cb.on_complete();
    return ERR_OK;
}
```

**Lines 1347–1356 — macOS bitrate calculation:**

```c
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
```

**Assessment:** The macOS-specific bitrate calculation is correctly guarded.

**Issues:**

- **MEDIUM:** `c->opts` is modified inside the per-file loop (line 1282,
  1328–1335, 1346). This mutates the shared `Converter` state between files
  in the same batch. If `converter_process_files()` is called twice (or
  concurrently), the option mutations from the first call will affect the
  second call.

- **MEDIUM:** `ensure_output_dir_writable()` is called once before the loop.
  If the output directory is deleted or becomes unwritable during the batch,
  there is no per-file check.

- **LOW:** `ERR_OUTPUT_EXISTS` is mapped to `ERR_SKIP_FILE` in the on_file_end
  callback (line 1263), which silently changes the error code. The caller
  receives `ERR_SKIP_FILE` but the actual reason was `ERR_OUTPUT_EXISTS`.

---

## 4. Platform-Independent Code Analysis

### What Works Well

The following code is correct, well-structured, and fully platform-agnostic:

| Component | Lines | Notes |
|---|---|---|
| `ConverterError` enum | h:13–40 | Clear error classification |
| `ConverterCallbacks` struct | h:88–127 | Clean callback interface |
| `codec_uses_mov_container()` | 35–40 | Pure string comparison |
| `audio_output_mode_is()` | 42–44 | Pure string comparison |
| `build_audio_filter_expr()` | 55–95 | **IMMUTABLE**, no platform code |
| `converter_create()` | 267–270 | Simple calloc |
| `converter_destroy()` | 272–275 | Simple free |
| `converter_set_callbacks()` | 280–289 | Simple struct copy |
| `converter_error_string()` | 347–364 | Clean switch/case |
| `parse_time_hms()` | 369–376 | Standard sscanf |
| `check_output_exists()` | 688–703 | stat() portable via MinGW |
| `peak_two_pass()` algorithm | 708–784 | **IMMUTABLE**, algorithm only |
| `loudnorm_two_pass()` algorithm | 796–904 | **IMMUTABLE**, algorithm only |
| `json_number_or_string_value()` | 789–793 | jansson API, portable |

### What Is Problematic (Cross-Platform)

| Component | Lines | Problem |
|---|---|---|
| `is_executable()` | 154–156 | POSIX `access(X_OK)` — broken on Windows |
| `get_exe_dir()` `#else` branch | 174–182 | `/proc/self/exe` — Linux only |
| `resolve_bundled_bin()` | 187–201 | `/` separator, static buffer |
| `get_ffmpeg_bin()` Windows path | 203–218 | Returns `""` on Windows |
| `get_ffprobe_bin()` Windows path | 220–235 | Returns `""` on Windows |
| `get_cpu_count()` `#else` | 250–254 | `sysconf()` — not on Windows |
| `mkdir_p()` | 393–422 | POSIX `mkdir(p, mode)` — broken on Windows |
| `ensure_output_dir_writable()` | 424–464 | `HOME` env, `W_OK`, `/` separator |
| `get_duration()` | 469–488 | `2>/dev/null`, unescaped paths |
| `build_ffmpeg_cmd()` | 909–1125 | Buffer overrun, no path escaping |
| `run_ffmpeg_encode_with_progress()` | 1130–1207 | Buffer truncation risk |

---

## 5. Platform-Specific Code Analysis

### 5.1 Linux-Specific Code

| Location | Lines | Code |
|---|---|---|
| Include | 11–13 | `#include "linux/runtime_probe.h"` |
| `converter_set_options()` | 298–331 | `LinuxCodecSupport`, `linux_probe_codec_support()`, VAAPI device detection |
| `get_exe_dir()` `#else` | 174–182 | `readlink("/proc/self/exe")` |
| `get_ffmpeg_bin()` | 210–212 | `linux_get_preferred_ffmpeg_bin()` |
| `get_ffprobe_bin()` | 227–229 | `linux_get_preferred_ffprobe_bin()` |
| `get_cpu_count()` `#else` | 250–254 | `sysconf(_SC_NPROCESSORS_ONLN)` |

Linux-specific includes and function calls are correctly guarded by
`#if defined(__linux__)`. The Linux binary resolution logic is delegated
to `src/platform/linux/runtime_probe.c` which implements a complete search
strategy: env vars → bundled → PATH.

### 5.2 macOS-Specific Code

| Location | Lines | Code |
|---|---|---|
| Includes | 14–21 | `<sys/sysctl.h>`, `<mach-o/dyld.h>`, `<math.h>` |
| `get_exe_dir()` macOS | 164–173 | `_NSGetExecutablePath()`, `realpath()` |
| `resolve_bundled_bin()` macOS | 195–198 | `../Resources/bin/` path |
| `get_cpu_count()` macOS | 241–249 | `sysctlbyname("hw.ncpu")` |
| VideoToolbox helpers | 493–553 | `get_video_info()`, `calc_hevc_vt_bitrate_kbps()` |
| Main loop macOS | 1347–1356 | Bitrate calculation via `get_video_info()` |

macOS-specific code is correctly guarded by `#if defined(__APPLE__)`.

### 5.3 Windows-Specific Code (Current State)

The only Windows-specific code currently in `converter.c`:

```c
// make_output_name() lines 595–599:
#ifdef _WIN32
    const char* backslash = strrchr(input, '\\');
    if (backslash && (!slash || backslash > slash))
        slash = backslash;
#endif
```

This is the **only** `#ifdef _WIN32` block in the entire file. It handles
Windows backslash in the basename extraction. Every other platform-specific
concern for Windows is **unhandled**.

---

## 6. Windows-Specific Code Issues

This section covers all issues that would prevent `converter.c` from
compiling or running correctly on Windows. The current development environment
uses **MSYS2 MinGW x64** for building and running on Windows.

### 6.1 Compilation Blockers

The following issues prevent the code from compiling at all on Windows without
MSYS2's POSIX compatibility layer:

#### Issue W-C1: Missing `<unistd.h>`

**Lines:** 7 (include), 155 (`access()`), 176 (`ssize_t`, `readlink()`),
247 (`sysconf()`), 251 (`sysconf()`), 457 (`access()`), 573 (`access()`)

`<unistd.h>` is not available on Windows MSVC. On MinGW/MSYS2, it is
available but provides a subset of POSIX functions, some of which have
different semantics.

**Minimum fix for MSYS2/MinGW:** Keep `<unistd.h>` but gate POSIX-only
function calls (`readlink`, `sysconf`) with platform guards.

#### Issue W-C2: Missing `<libgen.h>`

**Lines:** 10 (include), 170 (`dirname()`), 179 (`dirname()`)

`<libgen.h>` is not available on Windows MSVC. MinGW does not provide it.
`dirname()` is used in `get_exe_dir()` in both macOS and non-macOS branches.

**Fix:** Implement a portable `path_dirname()` helper using
`strrchr()` which is available everywhere.

#### Issue W-C3: POSIX `mkdir()` with Mode Argument

**Lines:** 412–419 (`mkdir(tmp, 0755)`)

On Windows, `mkdir()` from `<direct.h>` takes only one argument. The POSIX
two-argument form `mkdir(path, mode)` does not compile on MSVC. On MinGW,
`mkdir()` is mapped to `_mkdir()` which also takes one argument.

**Fix:**
```c
#ifdef _WIN32
    #include <direct.h>
    #define MKDIR(path) _mkdir(path)
#else
    #define MKDIR(path) mkdir(path, 0755)
#endif
```

#### Issue W-C4: `readlink()` and `/proc/self/exe`

**Lines:** 176–181

`readlink()` is POSIX and not available on Windows. `/proc/self/exe` does not
exist on Windows.

**Fix for Windows:**
```c
#ifdef _WIN32
    #include <windows.h>
    wchar_t exe_path_w[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path_w, MAX_PATH);
    // convert to UTF-8, then extract directory
#endif
```

#### Issue W-C5: `sysconf(_SC_NPROCESSORS_ONLN)`

**Lines:** 251

`sysconf()` is POSIX. On Windows, use:
```c
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#endif
```
Or as a simpler fallback: `atoi(getenv("NUMBER_OF_PROCESSORS") ? getenv("NUMBER_OF_PROCESSORS") : "1")`.

#### Issue W-C6: `ssize_t` Undefined

**Lines:** 176

`ssize_t` is a POSIX type defined in `<unistd.h>`. On Windows MSVC, it does
not exist. On MinGW it may be available as `_ssize_t`. The fix is to use
`int64_t` or simply remove the dependency by restructuring `get_exe_dir()`.

### 6.2 Runtime Failures on Windows

These issues will not prevent compilation (especially under MSYS2/MinGW)
but will cause incorrect behavior at runtime.

#### Issue W-R1: `get_exe_dir()` Returns Empty String

**Lines:** 158–185

On Windows, the `#else` branch (Linux readlink) is selected. `readlink()`
returns `len = -1` (fails). `exe_dir` remains empty. All downstream calls
to `resolve_bundled_bin()` return `NULL`, and `get_ffmpeg_bin()` returns `""`.

**Effect:** FFmpeg and ffprobe cannot be found. All file conversion fails.

#### Issue W-R2: `get_ffmpeg_bin()` Returns `""`

**Lines:** 203–218

Unless the user sets `FFMPEG` or `FFMPEG_BIN` environment variables,
`get_ffmpeg_bin()` returns an empty string. The constructed popen command
becomes:
```
"" -hide_banner -v error -encoders 2>/dev/null
```
which fails immediately.

**Effect:** All audio encoder detection fails; all FFmpeg operations fail.

#### Issue W-R3: `mkdir_p()` Fails on Windows-style Paths

**Lines:** 393–422

On Windows, `output_dir` may be `C:\Users\User\output`. The loop only
splits on `/`. It will try `mkdir("C:\\Users\\User\\output", 0755)` as a
single call, which will fail if any intermediate directory is missing.

**Effect:** Output directory creation fails → `converter_process_files()`
returns `ERR_INVALID_OPTIONS` for every file.

#### Issue W-R4: `HOME` Environment Variable Not Set

**Lines:** 435

On Windows, `getenv("HOME")` typically returns `NULL` (unless the user or
an IDE sets it explicitly). The fallback is `"."` (current directory), so
the default output directory becomes `./ffmpeg_converter`. This works but
creates the output folder in the working directory instead of the user's
home folder.

**Effect:** Default output directory is wrong.

#### Issue W-R5: `2>/dev/null` in Subprocess Commands

**Lines:** 118, 473–475, 507–511

These commands are executed via `popen()`. In MSYS2, `popen()` invokes
`/bin/sh` (bash). Under native Windows CMD/PowerShell, `popen()` invokes
`cmd.exe`.

- Under MSYS2 `sh`: `2>/dev/null` works correctly.
- Under native CMD: `2>nul` would be needed; `2>/dev/null` will print an
  error.

**Effect in MSYS2 context (current setup):** Works correctly.  
**Effect if application is packaged without MSYS2:** Fails.

#### Issue W-R6: `access(path, X_OK)` Incorrect Semantics

**Lines:** 155

On Windows, NTFS does not have POSIX execute permission bits. `access(path, X_OK)`
(or `_access(path, 0)` on MinGW) only checks if the file exists, not if it
can be executed. As a result, `is_executable()` returns `1` for ANY existing
file, including DLLs, text files, etc.

**Effect:** `resolve_bundled_bin()` may return a path to a non-executable
file, causing FFmpeg invocation to fail.

#### Issue W-R7: Stack Buffer Overflow Risk in `build_ffmpeg_cmd()`

**Lines:** 934–1124

The command buffer `char cmd[16384]` is filled using `strcat()` without
bounds checking. On Windows, paths are typically longer than on Linux/macOS
(e.g., `C:\Users\John Doe\Videos\My Footage 2024\`) and may include
spaces. While paths with spaces are quoted, the total command length can
exceed 16384 characters with deeply nested directories or very long filenames.

**Effect:** Stack corruption → undefined behavior → crash.

#### Issue W-R8: `run_ffmpeg_encode_with_progress()` Buffer Truncation

**Lines:** 1138

```c
char cmd[8192];
snprintf(cmd, sizeof(cmd), "%s 2>&1", cmd_base);
```

`cmd_base` can be up to 16384 bytes. `cmd[8192]` truncates anything beyond
8187 bytes. On Windows, long paths mean the total command is more likely
to approach or exceed 8192 bytes.

**Effect:** Truncated command → FFmpeg receives incomplete arguments →
encoding fails.

#### Issue W-R9: Character Encoding (UTF-8 vs. Windows Codepage)

**Lines:** all popen calls, all path handling

On Windows, the default process codepage may be CP1251, CP1252, or other ANSI
codepages depending on locale. File names and directory paths with non-ASCII
characters (e.g., Cyrillic, East Asian characters) will be encoded in the
system codepage when passed via `char*` to `popen()`.

- If the application is built as MSYS2/MinGW and uses the MSYS2 runtime, it
  operates in UTF-8 mode by default (MSYS2 sets the UTF-8 codepage).
- If the application is a native Windows executable that reads paths from the
  system, non-ASCII characters in paths may not be correctly passed to FFmpeg.

**Current environment:** MSYS2 provides UTF-8 compatibility. This is not a
critical issue in the MSYS2 build, but must be documented for native
Windows packaging.

### 6.3 Path Handling Comparison

| Scenario | Linux | macOS | Windows (MSYS2) | Windows (Native) |
|---|---|---|---|---|
| Separator in paths | `/` | `/` | Both `/` and `\` | `\` (canonical) |
| Home directory env | `HOME` | `HOME` | `HOME` (MSYS2) | `USERPROFILE` |
| Executable lookup | `/proc/self/exe` | `_NSGetExecutablePath` | GetModuleFileName | GetModuleFileName |
| Execute permission | `X_OK` via `access()` | `X_OK` via `access()` | File extension | File extension |
| Shell in popen | `/bin/sh` | `/bin/sh` | `/bin/sh` (MSYS2) | `cmd.exe` (native) |
| Null device | `/dev/null` | `/dev/null` | `/dev/null` (MSYS2) | `nul` |
| mkdir mode | `mkdir(p, 0755)` | `mkdir(p, 0755)` | `_mkdir(p)` | `_mkdir(p)` |
| Directory exists test | `stat() + S_ISDIR` | `stat() + S_ISDIR` | Same (MinGW) | `GetFileAttributes` |
| Write test | `access(W_OK)` | `access(W_OK)` | `_access(2)` | `CreateFile(WRITE)` |

### 6.4 Tool Path Discovery: Windows vs. Linux/macOS

This is the **fundamental architectural difference** between the Windows version
and Linux/macOS.

On **Linux** and **macOS**, there is a reliable, convention-based mechanism to
find tools:

- Linux: Tools are installed via package manager to a predictable location
  (`/usr/bin/ffmpeg`), and the project bundles its own copies next to the
  executable when needed. `runtime_probe.c` implements a comprehensive search:
  env vars → bundled (next to exe) → system PATH.
- macOS: The GUI `.app` bundle contains `ffmpeg`/`ffprobe` in
  `Contents/Resources/bin/`. MacPorts installs to `/opt/local/bin/`. A
  predictable search order exists.

On **Windows**, there is **no single standard tool installation path**:

| Tool | Where it may be installed | How to find it |
|---|---|---|
| `ffmpeg.exe` | MSYS2: `/mingw64/bin/ffmpeg.exe` → `C:\msys64\mingw64\bin\ffmpeg.exe` | Must be on PATH |
| `ffmpeg.exe` | Standalone download: `C:\ffmpeg\bin\ffmpeg.exe` | User-configured |
| `ffmpeg.exe` | Chocolatey: `C:\ProgramData\chocolatey\bin\ffmpeg.exe` | `choco install ffmpeg` |
| `ffmpeg.exe` | winget: varies | `winget install ffmpeg` |
| `mkvmerge.exe` | MSYS2: `/mingw64/bin/mkvmerge.exe` | MSYS2 package |
| `mkvmerge.exe` | MKVToolNix installer: `C:\Program Files\MKVToolNix\mkvmerge.exe` | Registry or PATH |
| `MP4Box.exe` | Chocolatey: `C:\ProgramData\chocolatey\bin\MP4Box.exe` | `choco install gpac` |
| `MP4Box.exe` | GPAC installer: `C:\Program Files\GPAC\MP4Box.exe` | Registry or PATH |

**Currently configured Windows environment:**
- FFmpeg (with all required filters: `soxr`, `libfdk_aac`, `loudnorm`,
  `volumedetect`, `deblock`, `asplit`) installed via MSYS2:
  `pacman -S mingw-w64-x86_64-ffmpeg`
- mkvmerge installed via MSYS2:
  `pacman -S mingw-w64-x86_64-mkvtoolnix`
- MP4Box installed via Chocolatey:
  `choco install gpac`

Because of this fragmentation, the Windows binary resolution logic **must**:

1. Check environment variables first (`FFMPEG`, `FFMPEG_BIN`, `FFPROBE_BIN`,
   `MKVMERGE_BIN`, `MP4BOX_BIN`) — this allows user override.
2. Check if the binary exists next to the executable (bundled deployment).
3. Search `PATH` for the binary by name + `.exe` extension.
4. If not found: report a clear error naming the missing tool and how to
   install it (MSYS2 package name or Chocolatey command).
5. **Never** silently return `""` and let the subprocess fail with a cryptic
   error.

This is in direct contrast to Linux where `linux_probe_codec_support()` in
`runtime_probe.c` implements a complete and well-structured binary search.
**Windows needs an equivalent `windows_probe_tools()` function.**

---

## 7. Data Structure Needs

The current data structures do not provide a place to store platform-specific
tool path information. The following additions are needed for a proper
Windows implementation:

### 7.1 Platform Tool Paths (currently missing)

On Linux, `LinuxCodecSupport` (in `runtime_probe.h`) carries resolved tool
paths. No equivalent exists for Windows or macOS in `ConvertOptions` or the
`Converter` struct.

```c
// Needed: platform-agnostic tool path storage inside Converter struct
struct Converter {
    ConvertOptions opts;
    ConverterCallbacks cb;
    int stop_flag;
    // --- Missing fields: ---
    char ffmpeg_path[1024];     // resolved at startup
    char ffprobe_path[1024];    // resolved at startup
    char mkvmerge_path[1024];   // resolved at startup, optional
    char mp4box_path[1024];     // resolved at startup, optional
};
```

### 7.2 Windows GPU Codec Availability (currently missing)

The `ConvertOptions` struct has `hw_device[1024]` for the VAAPI device path
on Linux. For Windows, the equivalent would be a detected NVENC/QSV
capability flag.

Currently there is no runtime detection of NVENC or QSV availability. Adding
it is optional (can be done based on the `ffmpeg -encoders` output), but
the data structure should be able to carry this information.

### 7.3 Platform Capability Flags (refactoring need)

After platform probing, the `Converter` struct should carry a capability
bitmap to avoid repeated probing:

```c
// Proposed capability flags
#define CAP_VAAPI_H264      (1 << 0)   // Linux
#define CAP_VAAPI_HEVC      (1 << 1)   // Linux
#define CAP_VIDEOTOOLBOX    (1 << 2)   // macOS
#define CAP_NVENC_H264      (1 << 3)   // Windows/Linux
#define CAP_NVENC_HEVC      (1 << 4)   // Windows/Linux
#define CAP_QSV_H264        (1 << 5)   // Windows/Linux
#define CAP_QSV_HEVC        (1 << 6)   // Windows/Linux
#define CAP_LIBFDK_AAC      (1 << 7)   // all platforms if built
#define CAP_AAC_AT          (1 << 8)   // macOS only
```

---

## 8. Refactoring Opportunities

### 8.1 Extract Platform Binary Resolution to Platform Files

The following functions should be moved to platform-specific source files
with a common interface header `converter_platform.h`:

| Function | Current File | Target File |
|---|---|---|
| `get_exe_dir()` | `converter.c` | `platform/linux/platform_linux.c` + `platform/macos/platform_macos.c` + `platform/windows/platform_windows.c` |
| `resolve_bundled_bin()` | `converter.c` | Platform files |
| `get_ffmpeg_bin()` | `converter.c` | Platform files |
| `get_ffprobe_bin()` | `converter.c` | Platform files |
| `is_executable()` | `converter.c` | Platform files (different semantics) |
| `get_cpu_count()` | `converter.c` | Platform files |

The platform interface header would declare:

```c
// converter_platform.h
const char* platform_get_ffmpeg_bin(void);
const char* platform_get_ffprobe_bin(void);
int         platform_get_cpu_count(void);
int         platform_is_executable(const char* path);
const char* platform_get_exe_dir(void);
```

### 8.2 Extract macOS VideoToolbox Code

The following are already guarded by `#if defined(__APPLE__)` but should be
physically moved to `platform/macos/`:

- `get_video_info()`
- `calc_hevc_vt_bitrate_kbps()`
- The bitrate calculation block in `converter_process_files()` (lines 1347–1356)

### 8.3 Extract Linux VAAPI Setup

The Linux VAAPI initialization block in `converter_set_options()` (lines
298–331) should move to a `platform_set_options_hook()` in
`platform/linux/platform_linux.c`:

```c
// platform_linux.c
ConverterError platform_configure_codec(
    const char* codec,
    char* hw_device, size_t hw_device_sz,
    int* hwaccel_enabled
);
```

### 8.4 Eliminate Buffer Overflow in `build_ffmpeg_cmd()`

Replace the `strcat()` approach with a bounded string builder:

```c
typedef struct {
    char* buf;
    size_t capacity;
    size_t len;
    int overflow;
} CmdBuf;

static void cmdbuf_append(CmdBuf* b, const char* s);
static void cmdbuf_append_quoted(CmdBuf* b, const char* s);
```

`cmdbuf_append_quoted()` must handle:
- Escaping embedded double quotes (platform-specific: `\"` on Unix,
  `\"` or `""` on Windows)
- Handling special characters in paths

### 8.5 Fix Buffer Size Mismatch Between `build_ffmpeg_cmd()` and `run_ffmpeg_encode_with_progress()`

`build_ffmpeg_cmd()` produces a command in a `16384`-byte buffer.
`run_ffmpeg_encode_with_progress()` wraps it in an `8192`-byte buffer with
`" 2>&1"` appended. The output buffer must be at least as large as the input:

```c
// Consistent sizes:
#define CMD_BUFFER_SIZE 32768   // consistent across all functions
```

### 8.6 Consolidate AAC Encoder Selection

The AAC encoder selection logic is repeated three times (lines 1051–1102).
Extract to a helper:

```c
static void append_aac_codec_flags(CmdBuf* cmd, int has_aac_at,
                                   int has_libfdk_aac, int fdk_vbr,
                                   int stream_index); // -1 for single stream
```

### 8.7 Add Windows-specific `mkdir_p()`

```c
// platform_windows.c
static int mkdir_p(const char* path) {
    // Use CreateDirectoryA() with proper separator handling
    // or SHCreateDirectoryExA()
}
```

### 8.8 Create `windows_probe_tools()`

Analogous to `linux_probe_codec_support()`, create a Windows-specific
binary resolution function:

```c
// platform_windows.c
typedef struct {
    char ffmpeg_bin[MAX_PATH];
    char ffprobe_bin[MAX_PATH];
    char mkvmerge_bin[MAX_PATH];
    char mp4box_bin[MAX_PATH];
    int has_nvenc_h264;
    int has_nvenc_hevc;
    int has_qsv_h264;
    int has_qsv_hevc;
    int has_libfdk_aac;
} WindowsToolPaths;

int windows_probe_tools(WindowsToolPaths* out);
```

Search order for each tool:
1. Environment variable (e.g., `FFMPEG`, `FFMPEG_BIN`)
2. Directory containing the application executable (`GetModuleFileName`)
3. System `PATH` (search for `.exe` extension)
4. If not found: record as empty string, report error to caller

---

## 9. Summary of Issues by Severity

### CRITICAL — Compilation Blockers on Windows

| ID | Lines | Issue |
|---|---|---|
| W-C1 | 7 | Unconditional `#include <unistd.h>` — not available on MSVC |
| W-C2 | 10 | Unconditional `#include <libgen.h>` — not available on Windows |
| W-C3 | 412, 419 | POSIX `mkdir(path, mode)` — Windows takes only `mkdir(path)` |
| W-C4 | 176–181 | `readlink("/proc/self/exe")` — Linux only, not Windows |
| W-C5 | 251 | `sysconf(_SC_NPROCESSORS_ONLN)` — POSIX only, not on Windows |
| W-C6 | 176 | `ssize_t` undefined on Windows without `<unistd.h>` |

### HIGH — Runtime Failures on Windows (Some Also on Linux/macOS)

| ID | Lines | Issue |
|---|---|---|
| W-R1 | 158–185 | `get_exe_dir()` always returns `""` on Windows → no binary found |
| W-R2 | 203–218 | `get_ffmpeg_bin()` returns `""` on Windows → all ops fail |
| W-R3 | 393–422 | `mkdir_p()` ignores `\` separators → output dir creation fails |
| W-R4 | 435 | `getenv("HOME")` returns NULL on Windows → wrong output dir |
| W-R5 | 118, 473, 507 | `2>/dev/null` in commands → fails in native CMD context |
| W-R6 | 155 | `access(path, X_OK)` accepts any file as "executable" on Windows |
| W-R7 | 934 | Stack buffer overflow risk in `build_ffmpeg_cmd()` with long paths |
| W-R8 | 1138 | `cmd[8192]` truncates `cmd_base` which can be 16384 bytes |
| W-R9 | all popen | Character encoding issues if not in MSYS2 UTF-8 context |
| ALL | 104–152 | Static state in `ffmpeg_encoder_available()` — not thread-safe |
| ALL | 191 | `static char path[]` in `resolve_bundled_bin()` — not thread-safe |

### MEDIUM — Portability and Correctness Issues

| ID | Lines | Issue |
|---|---|---|
| M1 | h:81 | `hevc_vt_bitrate_kbps` in `ConvertOptions` — macOS impl detail in shared header |
| M2 | h:38 | `ERR_POPEN_FAILED` / `ERR_PCLOSE_FAILED` expose POSIX API in public enum |
| M3 | 47 | `audio_output_mode_valid()` dereferences `mode` without null check |
| M4 | 257 | `get_filter_threads()` not declared `static` — unintended linkage |
| M5 | 663 | Output path always built with `/` separator even on Windows |
| M6 | 487 | `get_duration()` silently returns `0.0` on ffprobe failure |
| M7 | 877 | `strrchr(buf, '{')` fragile JSON extraction in loudnorm 2-pass |
| M8 | 1252–1335 | Per-file mutation of `c->opts` inside the processing loop |
| M9 | 1263 | `ERR_OUTPUT_EXISTS` silently changed to `ERR_SKIP_FILE` in callback |
| M10 | 298–331 | `converter_set_options()` validates Linux VAAPI but no macOS/Windows codec validation |
| M11 | 29 | `codec_is_linux_vaapi()` called unconditionally on all platforms |

---

## 10. Recommendations for Refactoring

### Priority 1 — Platform File Structure

Create a platform abstraction layer before any code changes:

```
src/
├── converter/
│   ├── converter.c            (common logic only — no platform code)
│   ├── converter.h            (public API — unchanged)
│   └── converter_platform.h  (NEW: platform interface declarations)
├── platform/
│   ├── linux/
│   │   ├── runtime_probe.c   (existing — unchanged)
│   │   └── runtime_probe.h   (existing — unchanged)
│   ├── macos/
│   │   ├── platform_macos.c  (NEW: exe_dir, cpu_count, video_info, bitrate_calc)
│   │   └── platform_macos.h  (NEW)
│   └── windows/
│       ├── platform_windows.c (NEW: exe_dir via GetModuleFileName,
│       │                             cpu_count via GetSystemInfo,
│       │                             mkdir_p with CreateDirectory,
│       │                             windows_probe_tools,
│       │                             is_executable via PathFileExists+.exe check)
│       └── platform_windows.h (NEW)
```

### Priority 2 — Fix Critical Windows Compilation Issues

In order:
1. Add `#ifdef _WIN32` guard around `<unistd.h>` and `<libgen.h>` includes.
2. Add Windows-specific `get_exe_dir()` using `GetModuleFileNameW()`.
3. Add Windows-specific `mkdir_p()` using `CreateDirectoryA()` with both
   `/` and `\` separator handling.
4. Add Windows-specific `get_cpu_count()` using `GetSystemInfo()`.
5. Add `windows_probe_tools()` for binary resolution.
6. Gate all `#if defined(__linux__)` binary resolution code behind Linux
   guard; add equivalent Windows code.

### Priority 3 — Fix Buffer Safety

1. Replace `strcat()` in `build_ffmpeg_cmd()` with a bounded `CmdBuf` builder.
2. Unify buffer sizes: `build_ffmpeg_cmd()` → `run_ffmpeg_encode_with_progress()`
   must use the same size constant (recommend `32768`).
3. Add path escaping for special characters.

### Priority 4 — Fix Thread Safety

1. Add `_Atomic int stop_flag` or use a mutex for `converter_stop()`.
2. Remove static mutable state from `ffmpeg_encoder_available()` and
   `resolve_bundled_bin()` — store resolved state in `Converter` struct.

### Priority 5 — Audio Rule Enforcement

Document and enforce the immutability rule for audio functions with comments:

```c
/*
 * ============================================================
 * IMMUTABLE: build_audio_filter_expr()
 * This function implements the core audio filter chain.
 * It is IDENTICAL on all platforms and must NOT be modified.
 * See: docs/CONVERTER_CODE_ANALYSIS.md — Section 3.3
 * ============================================================
 */
static void build_audio_filter_expr(...) { ... }
```

Apply the same comment to `peak_two_pass()` and `loudnorm_two_pass()`.

### Priority 6 — Windows Tool Discovery Documentation

Document the required setup for Windows in a user-facing file:
- FFmpeg + libsoxr + libfdk_aac via MSYS2: `pacman -S mingw-w64-x86_64-ffmpeg`
- mkvmerge via MSYS2: `pacman -S mingw-w64-x86_64-mkvtoolnix`
- MP4Box via Chocolatey: `choco install gpac`
- Environment variable fallback: `FFMPEG`, `FFMPEG_BIN`, `FFPROBE_BIN`, etc.

### Priority 7 — Validate Codec Names Against Platform

In `converter_set_options()`, add checks:
- Windows: reject `h264_vaapi`, `hevc_vaapi`, `prores_videotoolbox`,
  `hevc_videotoolbox`.
- Linux: reject `prores_videotoolbox`, `hevc_videotoolbox`.
- macOS: reject `h264_vaapi`, `hevc_vaapi`.

Return `ERR_INVALID_OPTIONS` with an explanatory message through `on_error`.

---

*End of CONVERTER_CODE_ANALYSIS.md*
