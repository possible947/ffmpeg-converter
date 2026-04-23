# MUX_WINDOWS_ADAPTATION_PLAN.md

Detailed architectural plan for adapting `src/mux/mux.c` to support Windows
while preserving full functionality on Linux and macOS.

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Current Architecture Analysis](#2-current-architecture-analysis)
   - 2.1 [Current File Structure](#21-current-file-structure)
   - 2.2 [Code Distribution](#22-code-distribution)
3. [Detailed Function-by-Function Analysis](#3-detailed-function-by-function-analysis)
   - 3.1 [Callback Functions (lines 16–32)](#31-callback-functions-lines-1632)
   - 3.2 [File Checking (lines 34–39)](#32-file-checking-lines-3439)
   - 3.3 [Shell Quoting (lines 41–102)](#33-shell-quoting-lines-41102)
   - 3.4 [FPS Parsing (lines 104–119)](#34-fps-parsing-lines-104119)
   - 3.5 [Video Rate Probing (lines 121–176)](#35-video-rate-probing-lines-121176)
   - 3.6 [Mux Output Validation (lines 178–214)](#36-mux-output-validation-lines-178214)
   - 3.7 [Mux Command Execution (lines 216–240)](#37-mux-command-execution-lines-216240)
   - 3.8 [Binary Resolution — ffprobe (lines 242–294)](#38-binary-resolution--ffprobe-lines-242294)
   - 3.9 [Binary Resolution — mkvmerge (lines 296–341)](#39-binary-resolution--mkvmerge-lines-296341)
   - 3.10 [Main MUX Function (lines 343–431)](#310-main-mux-function-lines-343431)
4. [Headers Analysis](#4-headers-analysis)
5. [Windows-Specific Implementation Details](#5-windows-specific-implementation-details)
   - 5.1 [File Existence Check](#51-file-existence-check)
   - 5.2 [Shell Quoting for Windows](#52-shell-quoting-for-windows)
   - 5.3 [Command Execution Wrapper](#53-command-execution-wrapper)
   - 5.4 [Binary Resolution on Windows](#54-binary-resolution-on-windows)
   - 5.5 [File Operations](#55-file-operations)
6. [Proposed New File Structure](#6-proposed-new-file-structure)
7. [Platform Abstraction Interface](#7-platform-abstraction-interface)
   - 7.1 [Common Wrapper Signatures](#71-common-wrapper-signatures)
   - 7.2 [Portability Classification Summary](#72-portability-classification-summary)
8. [CMakeLists.txt Changes](#8-cmakeliststxt-changes)
9. [Implementation Phases](#9-implementation-phases)
10. [Risk Assessment](#10-risk-assessment)

---

## 1. Executive Summary

### 1.1 Current State

`src/mux/mux.c` is the MKV multiplexing post-processing module. It merges a
separately-encoded audio track (intermediate file) with a video track file into
a single `.mkv` container using `mkvmerge`, and validates the result with
`ffprobe`. The module is **432 lines** of C and is covered by a 27-line header
`src/mux/mux.h`.

Current platform support:

| Platform | Status |
|----------|--------|
| Linux    | ✅ Full support (runtime binary resolution via `linux_get_preferred_*`) |
| macOS    | 🟡 Partial support (hardcoded candidate dirs; no App Bundle handling) |
| Windows  | ❌ Does not compile (POSIX-only headers and APIs) |

### 1.2 Task

Implement **full Windows support** for the MUX module while keeping Linux and
macOS behaviour identical to what it is today. The plan uses a platform
abstraction layer so that `mux.c` itself stays free of dense `#ifdef` blocks.

### 1.3 Scope

- **Source:** `src/mux/mux.c` (432 lines) + `src/mux/mux.h` (27 lines)
- **Goal:** MKV multiplexing of audio + video tracks on all three platforms
- **Approach:** Extract platform-specific logic into `src/mux/platform/`
  (mirrors the pattern used in `src/platform/`)

### 1.4 Code Distribution Overview

| Category | Approx. share | Description |
|----------|---------------|-------------|
| Portable logic | ~28% | Callbacks, suffix checks, FPS parsing |
| POSIX API | ~35% | `stat`, `popen`, `fgets` — wrappable |
| Linux-specific | ~23% | `get_mux_*_bin()` binary resolution |
| Shell operations | ~14% | `unlink`, `rename`, shell redirects |

---

## 2. Current Architecture Analysis

### 2.1 Current File Structure

```
src/mux/
├── mux.h          (27 lines — public API header)
└── mux.c          (432 lines — full implementation)
```

`mux.c` includes:

| Header | Purpose | Platform |
|--------|---------|----------|
| `<ctype.h>` | `tolower()` for suffix comparison | Portable |
| `<limits.h>` | `PATH_MAX` constant | Mostly portable (see §4) |
| `<stdio.h>` | `popen`, `fgets`, `snprintf` | Mostly portable |
| `<stdlib.h>` | `getenv`, `atof`, `strtok_r` | Mostly portable |
| `<string.h>` | `strlen`, `strcmp`, `strncpy` | Portable |
| `<sys/stat.h>` | `stat()`, `S_ISREG` | POSIX — wrap for Windows |
| `<sys/wait.h>` | `WIFEXITED`, `WEXITSTATUS` | POSIX — not on MSVC |
| `<unistd.h>` | `access()`, `unlink()` | POSIX — not on MSVC |
| `linux/runtime_probe.h` | `linux_get_preferred_*` | Linux-only |

### 2.2 Code Distribution

```
mux.c  (432 lines)
├── lines   1–14   includes & conditional linux header   (POSIX / platform)
├── lines  16–32   emit_*() callbacks                    (PORTABLE)
├── lines  34–39   file_exists_and_regular()             (POSIX — wrap)
├── lines  41–63   has_suffix_ci()                       (PORTABLE)
├── lines  65–71   video_track_needs_forced_fps()        (PORTABLE)
├── lines  73–102  shell_quote()                         (POSIX shell — adapt)
├── lines 104–119  parse_rate_to_fps()                   (PORTABLE)
├── lines 121–176  probe_video_rate_string()             (popen + shell — wrap)
├── lines 178–214  validate_mux_output()                 (popen + shell — wrap)
├── lines 216–240  run_mux_command()                     (popen + POSIX wait — wrap)
├── lines 242–294  get_mux_ffprobe_bin()                 (PLATFORM-SPECIFIC)
├── lines 296–341  get_mux_mkvmerge_bin()                (PLATFORM-SPECIFIC)
└── lines 343–431  mux_run_postprocess()                 (mixed — refactor)
```

---

## 3. Detailed Function-by-Function Analysis

### 3.1 Callback Functions (lines 16–32) — ALL PORTABLE

These three helpers forward events to the caller-supplied callback struct.
They contain no system calls and no platform-specific types.

| Function | Lines | Type | Action |
|----------|-------|------|--------|
| `emit_message()` | 16–20 | Pure logic | ✅ COPY AS-IS |
| `emit_error()` | 22–26 | Pure logic | ✅ COPY AS-IS |
| `emit_stage()` | 28–32 | Pure logic | ✅ COPY AS-IS |

---

### 3.2 File Checking (lines 34–39)

```c
static int file_exists_and_regular(const char* path)
{
    struct stat st;
    return path && path[0] != '\0' && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}
```

| Aspect | Detail |
|--------|--------|
| POSIX uses | `stat()`, `S_ISREG` (from `<sys/stat.h>`) |
| Windows problem | `<sys/stat.h>` exists in MinGW/MSVC but `S_ISREG` is not guaranteed |
| Action | 🟡 WRAP — introduce `platform_file_is_regular(path)` |

**Windows implementation:**

```c
/* mux/platform/mux_platform_windows.c */
int platform_file_is_regular(const char* path)
{
    DWORD attr;
    if (!path || path[0] == '\0') return 0;
    attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES) &&
           !(attr & FILE_ATTRIBUTE_DIRECTORY);
}
```

**POSIX implementation (Linux + macOS):**

```c
/* mux/platform/mux_platform_posix.c */
int platform_file_is_regular(const char* path)
{
    struct stat st;
    return path && path[0] != '\0' && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}
```

---

### 3.3 Shell Quoting (lines 41–102)

| Function | Lines | Type | Portability | Action |
|----------|-------|------|-------------|--------|
| `has_suffix_ci()` | 41–63 | Pure logic | ✅ COPY AS-IS | Simple case-insensitive suffix check |
| `video_track_needs_forced_fps()` | 65–71 | Pure logic | ✅ COPY AS-IS | Checks for `.hevc`/`.h265`/`.264`/`.h264` |
| `shell_quote()` | 73–102 | Shell quoting (POSIX single quotes) | 🟡 ADAPT | Works on Unix; Windows needs variation |

#### `shell_quote()` analysis

The current implementation wraps the string in POSIX single quotes and escapes
embedded single quotes as `'\''`:

```c
out[pos++] = '\'';
while (*input && pos + 5 < out_sz) {
    if (*input == '\'') {
        out[pos++] = '\''; out[pos++] = '\\';
        out[pos++] = '\''; out[pos++] = '\'';
    } else {
        out[pos++] = *input;
    }
    ++input;
}
out[pos++] = '\'';
```

On Windows `cmd.exe` does not understand single-quote quoting. The safe
approach for `cmd.exe` is double-quote wrapping with `"` escaped as `""`.
However, because the mux module runs `mkvmerge` and `ffprobe` — both of which
accept POSIX-style paths when running under MSYS2/MinGW environments — the
recommended solution is:

- **MSYS2 / MinGW shell** (`sh.exe` present): keep single-quote quoting and
  route commands through `sh -c "..."`.
- **Native cmd.exe** (no POSIX shell): switch to double-quote wrapping with
  `^` escaping for `cmd.exe` metacharacters.

**Solution:** Create `shell_quote_windows(input, out, out_sz)` that wraps in
double quotes and escapes `"` as `""`. The existing `shell_quote()` (POSIX) is
kept for Linux/macOS.

```c
/* Windows double-quote variant for cmd.exe */
static void shell_quote_windows(const char* input, char* out, size_t out_sz)
{
    size_t pos = 0;
    if (!out || out_sz < 3) { if (out && out_sz) out[0] = '\0'; return; }
    if (!input) input = "";
    out[pos++] = '"';
    while (*input && pos + 4 < out_sz) {
        if (*input == '"') {
            out[pos++] = '"'; out[pos++] = '"';
        } else {
            out[pos++] = *input;
        }
        ++input;
    }
    out[pos++] = '"';
    out[pos]   = '\0';
}
```

Platform dispatch macro in `mux.c`:

```c
#if defined(_WIN32)
#  define platform_shell_quote shell_quote_windows
#else
#  define platform_shell_quote shell_quote
#endif
```

---

### 3.4 FPS Parsing (lines 104–119)

```c
static double parse_rate_to_fps(const char* text)
```

Uses only `sscanf`, `atof`, and basic string logic. No platform dependencies.

| Action | ✅ COPY AS-IS |
|--------|--------------|

---

### 3.5 Video Rate Probing (lines 121–176)

```c
static int probe_video_rate_string(const char* ffprobe_bin,
                                   const char* input_file,
                                   char* rate_out, size_t rate_out_sz)
```

| POSIX dependency | Detail |
|-----------------|--------|
| `popen()` | Available on Windows (MinGW/MSVC CRT) as `_popen()` |
| `2>/dev/null` shell redirect | Windows equivalent is `2>nul` |
| `fgets()` | Portable |
| `pclose()` | Available as `_pclose()` on Windows |

**Action:** 🟡 WRAP — introduce `platform_run_command_read_line()` that hides
`popen`/`_popen` and the null-device redirect difference.

```c
/* mux/platform/mux_platform.h */
FILE* platform_popen(const char* cmd, const char* mode);
int   platform_pclose(FILE* fp);
const char* platform_null_redirect(void);  /* returns "2>/dev/null" or "2>nul" */
```

Usage in `probe_video_rate_string()`:

```c
snprintf(cmd, sizeof(cmd),
         "%s -v error -select_streams v:0 "
         "-show_entries stream=avg_frame_rate "
         "-of default=noprint_wrappers=1:nokey=1 %s %s",
         quoted_tool, quoted_input,
         platform_null_redirect());
fp = platform_popen(cmd, "r");
```

---

### 3.6 Mux Output Validation (lines 178–214)

```c
static int validate_mux_output(const char* ffprobe_bin, const char* output_file)
```

| POSIX dependency | Detail |
|-----------------|--------|
| `popen()` / `pclose()` | Wrap with `platform_popen` / `platform_pclose` |
| `2>/dev/null` redirect | Replace with `platform_null_redirect()` |
| `stat()` / `S_ISREG` | Replace with `platform_file_is_regular()` |
| `fgets()` | Portable |

**Action:** 🟡 WRAP — replace the three POSIX dependencies listed above with
platform wrappers. Logic otherwise stays the same.

---

### 3.7 Mux Command Execution (lines 216–240)

```c
static int run_mux_command(const char* cmd, const ConverterCallbacks* callbacks)
{
    FILE* fp;
    char line[1024];
    int rc;

    fp = popen(cmd, "r");
    ...
    rc = pclose(fp);
    if (WIFEXITED(rc))
        return WEXITSTATUS(rc);
    return -1;
}
```

| POSIX dependency | Windows problem | Solution |
|-----------------|-----------------|----------|
| `popen()` | MSVC: `_popen()`; MinGW: `popen()` exists | `platform_popen()` |
| `pclose()` | MSVC: `_pclose()` | `platform_pclose()` |
| `WIFEXITED` / `WEXITSTATUS` | Defined in `<sys/wait.h>` — **not available on MSVC** | See below |

`WIFEXITED` / `WEXITSTATUS` macros are not defined by MSVC. Under MSVC the
return value of `_pclose()` is the exit code directly (no signal encoding).
MinGW does provide these macros via `<sys/wait.h>`.

**Portable replacement:**

```c
/* mux/platform/mux_platform.h */
int platform_pclose_exitcode(FILE* fp);
/*
 * Closes the pipe and returns the process exit code, or -1 on error.
 * Hides WIFEXITED/WEXITSTATUS on POSIX and the direct return value on Windows.
 */
```

```c
/* mux/platform/mux_platform_posix.c */
int platform_pclose_exitcode(FILE* fp)
{
    int rc = pclose(fp);
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return -1;
}

/* mux/platform/mux_platform_windows.c */
int platform_pclose_exitcode(FILE* fp)
{
    return _pclose(fp);   /* returns exit code directly on Windows */
}
```

Updated `run_mux_command()`:

```c
static int run_mux_command(const char* cmd, const ConverterCallbacks* callbacks)
{
    FILE* fp = platform_popen(cmd, "r");
    char line[1024];
    if (!fp) return -1;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0') emit_message(callbacks, line);
    }
    return platform_pclose_exitcode(fp);
}
```

---

### 3.8 Binary Resolution — ffprobe (lines 242–294)

```c
static const char* get_mux_ffprobe_bin(void)
{
#if defined(__linux__)
    return linux_get_preferred_ffprobe_bin();
#else
    /* macOS: hardcoded candidate dirs + PATH search */
    ...
#endif
}
```

| Platform | Implementation |
|----------|----------------|
| Linux | `linux_get_preferred_ffprobe_bin()` (from `runtime_probe.h`) |
| macOS | Hardcoded dirs: `/opt/local/bin`, `/opt/homebrew/bin`, `/usr/local/bin` + PATH |
| Windows | ❌ Missing — falls into macOS branch (wrong paths, wrong separator) |

**Action:** 🔴 REWRITE with three-way platform split.

```c
static const char* get_mux_ffprobe_bin(void)
{
#if defined(__linux__)
    return linux_get_preferred_ffprobe_bin();
#elif defined(_WIN32)
    return windows_get_preferred_ffprobe_bin();
#else
    /* macOS — existing code unchanged */
    ...
#endif
}
```

**Platform-specific binary search matrix:**

| Tool | Linux | macOS | Windows |
|------|-------|-------|---------|
| `ffprobe` | `linux_get_preferred_ffprobe_bin()` | PATH search (hardcoded dirs) | `windows_get_preferred_ffprobe_bin()` |
| `mkvmerge` | `linux_get_preferred_mkvmerge_bin()` | PATH search (hardcoded dirs) | `windows_get_preferred_mkvmerge_bin()` |

---

### 3.9 Binary Resolution — mkvmerge (lines 296–341)

```c
static const char* get_mux_mkvmerge_bin(void)
{
#if defined(__linux__)
    return linux_get_preferred_mkvmerge_bin();
#else
    /* macOS: hardcoded candidate dirs + PATH search */
    ...
#endif
}
```

Same pattern as `get_mux_ffprobe_bin()`. Windows needs its own resolution
function that searches:

1. `MKVMERGE_BIN` environment variable
2. Bundled copy next to the executable (`GetModuleFileNameW`)
3. Common Windows package manager locations:
   - `C:\msys64\usr\bin\mkvmerge.exe` (MSYS2)
   - `C:\ProgramData\chocolatey\bin\mkvmerge.exe` (Chocolatey)
   - `%LOCALAPPDATA%\Programs\MkvToolNix\mkvmerge.exe`
   - `C:\Program Files\MkvToolNix\mkvmerge.exe`
4. System `PATH` (split on `;`, check for `mkvmerge.exe`)

**Action:** 🔴 REWRITE with three-way platform split (same structure as
`get_mux_ffprobe_bin()`).

---

### 3.10 Main MUX Function (lines 343–431)

```c
ConverterError mux_run_postprocess(
    const MuxOptions* opts,
    const ConvertOptions* convert_opts,
    const ConverterCallbacks* callbacks)
```

| Call | Line | Type | Platform-specific | Fix |
|------|------|------|-------------------|-----|
| `get_mux_ffprobe_bin()` | 374 | Platform-specific | Yes | § 3.8 |
| `get_mux_mkvmerge_bin()` | 375 | Platform-specific | Yes | § 3.9 |
| `unlink(temp_output)` | 382 | File deletion | ✅ `_unlink()` on Windows | `platform_unlink()` |
| `probe_video_rate_string()` | 386 | Wrapped popen | Yes | § 3.5 |
| `shell_quote()` | 393–396 | Shell quoting | Yes | § 3.3 |
| `run_mux_command()` | 411 | Wrapped popen | Yes | § 3.7 |
| `unlink(temp_output)` | 412, 420 | File deletion | Yes | `platform_unlink()` |
| `validate_mux_output()` | 417 | Wrapped popen | Yes | § 3.6 |
| `rename()` | 423 | File rename | ✅ Works on Windows (CRT) | No change needed |

`mux_run_postprocess()` itself contains **no unreachable platform logic** — all
platform-specific calls are delegated to the helper functions listed above.
After applying § 3.5–3.9 fixes the function needs only `unlink` → 
`platform_unlink()` and `shell_quote` → `platform_shell_quote` substitutions.

---

## 4. Headers Analysis

### 4.1 Problem Headers

| Header | Line | Type | Platform | Windows Handling |
|--------|------|------|----------|------------------|
| `<sys/stat.h>` | 8 | POSIX | Linux + macOS + partial MinGW | Replace `stat()`/`S_ISREG` with `platform_file_is_regular()` |
| `<sys/wait.h>` | 9 | POSIX | Linux + macOS only | Move `WIFEXITED`/`WEXITSTATUS` into `mux_platform_posix.c` |
| `<unistd.h>` | 10 | POSIX | Linux + macOS only | Move `access()`, `unlink()` into platform files |
| `linux/runtime_probe.h` | 13–14 | Linux | Linux only (guarded) | Add `_WIN32` guard with `windows/mux_probe_windows.h` |

### 4.2 Safe Headers (keep in mux.c)

| Header | Portability | Notes |
|--------|-------------|-------|
| `<ctype.h>` | ✅ Portable | Standard C |
| `<limits.h>` | ✅ Portable | `PATH_MAX` not guaranteed on Windows — define fallback |
| `<stdio.h>` | ✅ Portable | `snprintf`, `fgets` |
| `<stdlib.h>` | ✅ Portable | `getenv`, `atof`, `strtok_r`* |
| `<string.h>` | ✅ Portable | Standard C |

> **Note on `PATH_MAX`:** MSVC does not define `PATH_MAX` in `<limits.h>`.
> Add a fallback at the top of `mux.c`:
>
> ```c
> #ifndef PATH_MAX
> #  define PATH_MAX 4096
> #endif
> ```

> **Note on `strtok_r`:** MSVC does not provide `strtok_r`; use `strtok_s`
> (same signature, different name). The platform header should provide a
> compatibility macro:
>
> ```c
> #if defined(_MSC_VER)
> #  define strtok_r strtok_s
> #endif
> ```

### 4.3 Proposed include structure for mux.c after refactoring

```c
/* Standard C — always safe */
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PATH_MAX fallback */
#ifndef PATH_MAX
#  define PATH_MAX 4096
#endif

/* Platform abstraction layer — replaces POSIX-specific includes */
#include "mux_platform.h"

/* Platform-specific runtime probe headers */
#if defined(__linux__)
#  include "linux/runtime_probe.h"
#elif defined(_WIN32)
#  include "windows/mux_probe_windows.h"
#endif
```

---

## 5. Windows-Specific Implementation Details

### 5.1 File Existence Check

Replace `stat()` + `S_ISREG` with Win32 `GetFileAttributesA()`:

```c
/* src/mux/platform/mux_platform_windows.c */
#include <windows.h>
#include "../mux_platform.h"

int platform_file_is_regular(const char* path)
{
    DWORD attr;
    if (!path || path[0] == '\0') return 0;
    attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES) &&
           !(attr & FILE_ATTRIBUTE_DIRECTORY);
}
```

### 5.2 Shell Quoting for Windows

See § 3.3. The `shell_quote_windows()` function wraps with double quotes and
escapes embedded double quotes as `""`.

For MSYS2 environments the existing POSIX `shell_quote()` can be kept if
commands are routed through `sh.exe`. The recommended approach is to detect at
runtime whether `sh.exe` is available:

```c
/* windows_shell_mode: SHELL_CMD (cmd.exe) or SHELL_SH (sh.exe) */
static int windows_has_posix_shell(void)
{
    /*
     * Check for sh.exe in PATH first, then fall back to the default MSYS2
     * installation path.  The MSYS2 root can also be overridden via the
     * MSYS2_ROOT environment variable or located in the registry under
     * HKEY_LOCAL_MACHINE\SOFTWARE\MSYS2 to avoid hardcoding.
     */
    const char* msys2_root = getenv("MSYS2_ROOT");
    char sh_path[4096];

    if (_access("sh", 0) == 0) return 1;

    if (msys2_root && msys2_root[0] != '\0') {
        snprintf(sh_path, sizeof(sh_path), "%s\\usr\\bin\\sh.exe", msys2_root);
        if (_access(sh_path, 0) == 0) return 1;
    }
    /* Fallback to the most common default MSYS2 installation path */
    return _access("C:\\msys64\\usr\\bin\\sh.exe", 0) == 0;
}
```

### 5.3 Command Execution Wrapper

```c
/* src/mux/platform/mux_platform_windows.c */
FILE* platform_popen(const char* cmd, const char* mode)
{
    return _popen(cmd, mode);
}

int platform_pclose_exitcode(FILE* fp)
{
    return _pclose(fp);  /* returns process exit code directly */
}

int platform_unlink(const char* path)
{
    return _unlink(path);
}

const char* platform_null_redirect(void)
{
    return "2>nul";
}
```

```c
/* src/mux/platform/mux_platform_posix.c */
#include <sys/wait.h>
#include <unistd.h>

FILE* platform_popen(const char* cmd, const char* mode)  { return popen(cmd, mode); }

int platform_pclose_exitcode(FILE* fp)
{
    int rc = pclose(fp);
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return -1;
}

int platform_unlink(const char* path)  { return unlink(path); }
const char* platform_null_redirect(void)  { return "2>/dev/null"; }
```

### 5.4 Binary Resolution on Windows

```c
/* src/mux/windows/mux_probe_windows.h */
const char* windows_get_preferred_ffprobe_bin(void);
const char* windows_get_preferred_mkvmerge_bin(void);
```

```c
/* src/mux/windows/mux_probe_windows.c */
#include <windows.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

static int win_exe_exists(const char* path)
{
    return _access(path, 0) == 0;
}

const char* windows_get_preferred_ffprobe_bin(void)
{
    static char resolved[4096];
    const char* env;
    const char* candidates[] = {
        /*
         * Search priority: MSYS2 (most common dev environment) → Chocolatey
         * (package manager) → portable FFmpeg distribution.  Extend this list
         * to cover additional installation paths (e.g. Scoop, WinGet bundles)
         * as needed.  All paths can be overridden via the FFPROBE / FFPROBE_BIN
         * environment variables checked above.
         */
        "C:\\msys64\\usr\\bin\\ffprobe.exe",
        "C:\\msys64\\mingw64\\bin\\ffprobe.exe",
        "C:\\ProgramData\\chocolatey\\bin\\ffprobe.exe",
        "C:\\tools\\ffmpeg\\bin\\ffprobe.exe"
    };
    size_t i;
    const char* path_env;
    char path_copy[32768];
    char* ctx = NULL;
    char* dir;

    /*
     * NOTE — thread safety: `resolved` is a function-local static buffer.
     * These functions are intended to be called once during initialisation and
     * their results cached by the caller.  Calling them concurrently from
     * multiple threads without external synchronisation is not supported.
     * If thread-safe usage is required, replace the static buffer with a
     * caller-supplied output buffer (similar to the POSIX implementation).
     */
    env = getenv("FFPROBE");
    if (env && env[0] != '\0' && win_exe_exists(env)) return env;
    env = getenv("FFPROBE_BIN");
    if (env && env[0] != '\0' && win_exe_exists(env)) return env;

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
        if (win_exe_exists(candidates[i])) return candidates[i];

    path_env = getenv("PATH");
    if (!path_env || path_env[0] == '\0') return "ffprobe";

    strncpy(path_copy, path_env, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    dir = strtok_s(path_copy, ";", &ctx);
    while (dir) {
        if (dir[0] != '\0') {
            snprintf(resolved, sizeof(resolved), "%s\\ffprobe.exe", dir);
            if (win_exe_exists(resolved)) return resolved;
            snprintf(resolved, sizeof(resolved), "%s\\ffprobe", dir);
            if (win_exe_exists(resolved)) return resolved;
        }
        dir = strtok_s(NULL, ";", &ctx);
    }
    return "ffprobe";
}

const char* windows_get_preferred_mkvmerge_bin(void)
{
    static char resolved[4096];
    const char* env;
    const char* candidates[] = {
        "C:\\msys64\\usr\\bin\\mkvmerge.exe",
        "C:\\ProgramData\\chocolatey\\bin\\mkvmerge.exe",
        "C:\\Program Files\\MkvToolNix\\mkvmerge.exe",
        "C:\\Program Files (x86)\\MkvToolNix\\mkvmerge.exe"
    };
    size_t i;
    const char* path_env;
    char path_copy[32768];
    char* ctx = NULL;
    char* dir;

    env = getenv("MKVMERGE_BIN");
    if (env && env[0] != '\0' && win_exe_exists(env)) return env;

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
        if (win_exe_exists(candidates[i])) return candidates[i];

    path_env = getenv("PATH");
    if (!path_env || path_env[0] == '\0') return "mkvmerge";

    strncpy(path_copy, path_env, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    dir = strtok_s(path_copy, ";", &ctx);
    while (dir) {
        if (dir[0] != '\0') {
            snprintf(resolved, sizeof(resolved), "%s\\mkvmerge.exe", dir);
            if (win_exe_exists(resolved)) return resolved;
            snprintf(resolved, sizeof(resolved), "%s\\mkvmerge", dir);
            if (win_exe_exists(resolved)) return resolved;
        }
        dir = strtok_s(NULL, ";", &ctx);
    }
    return "mkvmerge";
}
```

### 5.5 File Operations

| POSIX call | Windows replacement | Notes |
|------------|--------------------|----|
| `unlink(path)` | `_unlink(path)` or `DeleteFileA(path)` | Available in `<io.h>` (MinGW/MSVC) |
| `rename(old, new)` | `rename(old, new)` | ✅ C standard — works unchanged |
| `access(path, X_OK)` | `_access(path, 0)` + `.exe` extension check | `X_OK` not meaningful on Windows |

---

## 6. Proposed New File Structure

```
src/mux/
├── mux.h                            (unchanged public API)
├── mux.c                            (refactored — platform calls through mux_platform.h)
├── mux_platform.h                   (NEW — platform abstraction header)
├── platform/
│   ├── mux_platform_posix.c         (NEW — POSIX impl: popen, pclose, unlink, stat)
│   └── mux_platform_windows.c       (NEW — Windows impl: _popen, _pclose, _unlink, GetFileAttributes)
└── windows/
    ├── mux_probe_windows.h          (NEW — Windows binary resolution API)
    └── mux_probe_windows.c          (NEW — Windows ffprobe + mkvmerge resolution)
```

### 6.1 mux_platform.h

```c
/* src/mux/mux_platform.h */
#ifndef FFMPEG_CONVERTER_MUX_PLATFORM_H
#define FFMPEG_CONVERTER_MUX_PLATFORM_H

#include <stdio.h>

/* File system */
int         platform_file_is_regular(const char* path);
int         platform_unlink(const char* path);

/* Process execution */
FILE*       platform_popen(const char* cmd, const char* mode);
int         platform_pclose_exitcode(FILE* fp);

/* Shell redirect for stderr suppression */
const char* platform_null_redirect(void);

/* Shell quoting */
void        platform_shell_quote(const char* input, char* out, size_t out_sz);

/* strtok_r / strtok_s compatibility */
#if defined(_MSC_VER)
#  define strtok_r strtok_s
#endif

/* PATH_MAX fallback */
#ifndef PATH_MAX
#  define PATH_MAX 4096
#endif

#endif /* FFMPEG_CONVERTER_MUX_PLATFORM_H */
```

---

## 7. Platform Abstraction Interface

### 7.1 Common Wrapper Signatures

| Wrapper | POSIX impl | Windows impl |
|---------|-----------|--------------|
| `platform_file_is_regular(path)` | `stat()` + `S_ISREG` | `GetFileAttributesA()` |
| `platform_popen(cmd, mode)` | `popen()` | `_popen()` |
| `platform_pclose_exitcode(fp)` | `pclose()` + `WIFEXITED`/`WEXITSTATUS` | `_pclose()` (direct exit code) |
| `platform_unlink(path)` | `unlink()` | `_unlink()` |
| `platform_null_redirect()` | `"2>/dev/null"` | `"2>nul"` |
| `platform_shell_quote(in,out,sz)` | single-quote POSIX | double-quote cmd.exe |

### 7.2 Portability Classification Summary

| Function | Portability | Action |
|----------|-------------|--------|
| `emit_message()` | ✅ Portable | Copy as-is |
| `emit_error()` | ✅ Portable | Copy as-is |
| `emit_stage()` | ✅ Portable | Copy as-is |
| `file_exists_and_regular()` | 🟡 POSIX | Replace with `platform_file_is_regular()` |
| `has_suffix_ci()` | ✅ Portable | Copy as-is |
| `video_track_needs_forced_fps()` | ✅ Portable | Copy as-is |
| `shell_quote()` | 🟡 POSIX shell | `platform_shell_quote()` dispatch |
| `parse_rate_to_fps()` | ✅ Portable | Copy as-is |
| `probe_video_rate_string()` | 🟡 POSIX popen | Replace with platform wrappers |
| `validate_mux_output()` | 🟡 POSIX popen | Replace with platform wrappers |
| `run_mux_command()` | 🟡 POSIX wait | Replace with `platform_pclose_exitcode()` |
| `get_mux_ffprobe_bin()` | 🔴 Platform-specific | Three-way `#if` split |
| `get_mux_mkvmerge_bin()` | 🔴 Platform-specific | Three-way `#if` split |
| `mux_run_postprocess()` | 🟡 Mixed | Replace `unlink` + `shell_quote` calls |

---

## 8. CMakeLists.txt Changes

The following source files must be added to the `CMakeLists.txt` target that
builds the mux module:

```cmake
# Platform sources for the mux module
if(WIN32)
    target_sources(ffmpeg_converter_lib PRIVATE
        src/mux/platform/mux_platform_windows.c
        src/mux/windows/mux_probe_windows.c
    )
else()
    target_sources(ffmpeg_converter_lib PRIVATE
        src/mux/platform/mux_platform_posix.c
    )
endif()
```

Additionally, add the include path for the new platform header:

```cmake
target_include_directories(ffmpeg_converter_lib PRIVATE
    src/mux
)
```

---

## 9. Implementation Phases

### Phase 1 — Platform Abstraction Layer (no functional change)

1. Create `src/mux/mux_platform.h` with all wrapper signatures.
2. Create `src/mux/platform/mux_platform_posix.c` — lift POSIX code from
   `mux.c` into the new file; keep existing behaviour.
3. Verify Linux + macOS builds and tests pass unchanged.

### Phase 2 — Refactor mux.c (no functional change)

4. Replace direct POSIX calls in `mux.c` with `platform_*()` wrappers.
5. Replace `#include <sys/stat.h>`, `<sys/wait.h>`, `<unistd.h>` with
   `#include "mux_platform.h"`.
6. Add `PATH_MAX` and `strtok_r` compatibility guards.
7. Verify Linux + macOS builds and tests pass unchanged.

### Phase 3 — Windows Binary Resolution

8. Create `src/mux/windows/mux_probe_windows.h` and
   `src/mux/windows/mux_probe_windows.c` with
   `windows_get_preferred_ffprobe_bin()` and
   `windows_get_preferred_mkvmerge_bin()`.
9. Add `#elif defined(_WIN32)` guards in `get_mux_ffprobe_bin()` and
   `get_mux_mkvmerge_bin()` in `mux.c`.

### Phase 4 — Windows Platform Layer

10. Create `src/mux/platform/mux_platform_windows.c` with all Win32-backed
    implementations of `platform_*()` functions.
11. Update `CMakeLists.txt` to select the correct platform source.

### Phase 5 — Build and Test on Windows

12. Compile with MSVC and MinGW.
13. Run integration test: mux a known audio intermediate + video track, verify
    output MKV contains both streams.
14. Test environment-variable overrides (`FFPROBE`, `FFPROBE_BIN`,
    `MKVMERGE_BIN`) resolve correctly.
15. Test that missing tools produce clear `ERR_INVALID_OPTIONS` errors rather
    than silent failures.

---

## 10. Risk Assessment

| Risk | Severity | Likelihood | Mitigation |
|------|----------|------------|------------|
| `_popen` on Windows spawns cmd.exe, breaking shell quoting | HIGH | HIGH | Use `sh -c` when MSYS2 shell available; otherwise native cmd.exe quoting |
| `2>nul` redirect absorbed by cmd.exe before mkvmerge sees args | MEDIUM | LOW | Append redirect at end of full command string, not inside quoted arg |
| `rename()` fails on Windows when dest file exists | HIGH | MEDIUM | Call `_unlink(dest)` before `rename()` on Windows |
| `PATH_MAX` undefined on MSVC | LOW | HIGH | Add `#ifndef PATH_MAX` fallback (§ 4.2) |
| `strtok_r` not available on MSVC | LOW | HIGH | Alias `strtok_s` as `strtok_r` (§ 4.2) |
| MSYS2 ffprobe not in standard Windows PATH | MEDIUM | HIGH | Search MSYS2 dirs explicitly (§ 5.4) |
| mkvmerge installed via MkvToolNix GUI (not MSYS2) | MEDIUM | MEDIUM | Search `Program Files\MkvToolNix` (§ 5.4) |
| Windows line endings (`\r\n`) in popen output | LOW | HIGH | Existing `strcspn(line, "\r\n")` already strips both — no change needed |
