# RUNTIME_PROBE_ANALYSIS_AND_PLAN.md

Detailed function-by-function analysis of `src/platform/linux/runtime_probe.c`
and `src/platform/linux/runtime_probe.h`, with a complete architectural plan
for extending runtime probing to Windows and macOS.

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Current Architecture Analysis](#2-current-architecture-analysis)
   - 2.1 [Current File Structure](#21-current-file-structure)
   - 2.2 [API Functions (from runtime_probe.h)](#22-api-functions-from-runtime_probeh)
   - 2.3 [Main Logical Sections in runtime_probe.c](#23-main-logical-sections-in-runtime_probec)
3. [Detailed Function-by-Function Analysis](#3-detailed-function-by-function-analysis)
   - 3.1 [Helper Functions (lines 18–44)](#31-helper-functions-lines-1844)
   - 3.2 [Binary Path Resolution (lines 46–182)](#32-binary-path-resolution-lines-46182)
   - 3.3 [GPU Detection (lines 184–280)](#33-gpu-detection-lines-184280)
   - 3.4 [Public API (lines 282–332)](#34-public-api-lines-282332)
4. [Linux-Specific Code Identification](#4-linux-specific-code-identification)
   - 4.1 [Critical POSIX Dependencies](#41-critical-posix-dependencies)
   - 4.2 [Headers Analysis](#42-headers-analysis)
   - 4.3 [Portability Classification by Function](#43-portability-classification-by-function)
5. [Required Windows Implementation](#5-required-windows-implementation)
   - 5.1 [Executable Directory on Windows](#51-executable-directory-on-windows)
   - 5.2 [PATH Search on Windows](#52-path-search-on-windows)
   - 5.3 [Executable File Check on Windows](#53-executable-file-check-on-windows)
   - 5.4 [GPU Detection on Windows](#54-gpu-detection-on-windows)
   - 5.5 [Windows Data Structure](#55-windows-data-structure)
6. [Required macOS Implementation](#6-required-macos-implementation)
   - 6.1 [Executable Directory on macOS](#61-executable-directory-on-macos)
   - 6.2 [PATH Search on macOS](#62-path-search-on-macos)
   - 6.3 [Executable File Check on macOS](#63-executable-file-check-on-macos)
   - 6.4 [GPU Detection on macOS](#64-gpu-detection-on-macos)
   - 6.5 [macOS Data Structure](#65-macos-data-structure)
7. [Proposed New File Structure](#7-proposed-new-file-structure)
8. [Platform Abstraction Interface](#8-platform-abstraction-interface)
   - 8.1 [Common Header: runtime_probe.h](#81-common-header-runtime_probeh)
   - 8.2 [Common Data Structure: RuntimeProbeResult](#82-common-data-structure-runtimeproberesult)
   - 8.3 [Portable Helper Functions](#83-portable-helper-functions)
9. [CMakeLists.txt Changes](#9-cmakeliststxt-changes)
10. [Implementation Phases](#10-implementation-phases)
11. [Risk Assessment](#11-risk-assessment)

---

## 1. Executive Summary

### 1.1 Current State

`src/platform/linux/runtime_probe.c` and its header are the sole implementation
of runtime probing in the project. The module is **332 lines** of C and lives
exclusively under `src/platform/linux/`, reflecting its Linux-only scope.

It serves two critical roles in the converter pipeline:

1. **Binary resolution** — finds `ffmpeg`, `ffprobe`, `mkvmerge`, and `MP4Box`
   by checking environment variables, bundled copies (next to the executable),
   and finally the system `PATH`.
2. **GPU detection** — scans `/dev/dri/renderD*` nodes and probes VAAPI hardware
   encoder availability (`h264_vaapi`, `hevc_vaapi`) by running a short test
   encode via `ffmpeg`.

Because both functions are guarded by `#if defined(__linux__)` at the call
sites in `converter.c` and `main.c`, neither works on Windows or macOS.

### 1.2 Task

Extend runtime probing to **Windows** and **macOS** so that all three platforms
can resolve tool binaries and detect available GPU encoders through a uniform
API. The Linux implementation is the reference; the Windows and macOS versions
must provide the same observable behaviour using platform-appropriate APIs.

### 1.3 Usefulness of runtime_probe

| Capability | Value |
|---|---|
| Binary resolution | Allows the converter to ship without bundled ffmpeg while still finding system-installed or bundled copies automatically |
| Environment variable override | Enables CI, packaging tools, and power users to force a specific binary path |
| GPU encoder detection | Selects the best hardware encoder at runtime without compile-time configuration |
| Zero-configuration startup | New users get a working conversion without any setup |

### 1.4 Refactoring Complexity

| Area | Complexity | Reason |
|---|---|---|
| Helper functions (is_executable_file, copy_string, starts_with) | LOW | Mostly portable; minor Windows adjustments |
| Binary resolution (resolve_path_binary) | MEDIUM | PATH separator and path-join separator differ on Windows |
| Executable directory (get_process_dir) | HIGH | Three completely different OS APIs required |
| GPU detection | HIGH | Entirely different GPU subsystems per platform |
| Public API surface | LOW | Rename + wrapper layer; callers need updating |

---

## 2. Current Architecture Analysis

### 2.1 Current File Structure

```
src/platform/linux/
├── runtime_probe.h   (36 lines)  — public API: types + 9 function declarations
└── runtime_probe.c   (332 lines) — full implementation, Linux-only
```

There are no equivalent files under `src/platform/macos/` or
`src/platform/windows/`. All GPU/binary detection for macOS and Windows is
either absent or handled ad hoc with `#ifdef` blocks at the call site.

### 2.2 API Functions (from runtime_probe.h)

#### Data Structure

```c
typedef struct {
    int  has_h264_vaapi;            // 1 if h264_vaapi encoder is usable
    int  has_hevc_vaapi;            // 1 if hevc_vaapi encoder is usable
    char default_render_node[1024]; // e.g. "/dev/dri/renderD128"
    char ffmpeg_bin[1024];          // resolved path to ffmpeg
    char ffprobe_bin[1024];         // resolved path to ffprobe
    char mkvmerge_bin[1024];        // resolved path to mkvmerge
    char mp4box_bin[1024];          // resolved path to MP4Box
    int  using_bundled_ffmpeg;      // 1 if ffmpeg found next to executable
    int  using_bundled_ffprobe;     // 1 if ffprobe found next to executable
    int  using_bundled_mkvmerge;    // 1 if mkvmerge found next to executable
    int  using_bundled_mp4box;      // 1 if MP4Box found next to executable
} LinuxCodecSupport;
```

#### Declared Functions

| Function | Return | Purpose |
|---|---|---|
| `linux_probe_codec_support(out)` | `int` (1 = ok) | Run full probe; populate cache; fill `*out` |
| `linux_is_bundled_ffmpeg_available()` | `int` | 1 if bundled ffmpeg found |
| `linux_is_bundled_ffprobe_available()` | `int` | 1 if bundled ffprobe found |
| `linux_is_bundled_mkvmerge_available()` | `int` | 1 if bundled mkvmerge found |
| `linux_is_bundled_mp4box_available()` | `int` | 1 if bundled MP4Box found |
| `linux_get_preferred_ffmpeg_bin()` | `const char *` | Best ffmpeg path (triggers probe if needed) |
| `linux_get_preferred_ffprobe_bin()` | `const char *` | Best ffprobe path |
| `linux_get_preferred_mkvmerge_bin()` | `const char *` | Best mkvmerge path |
| `linux_get_preferred_mp4box_bin()` | `const char *` | Best MP4Box path |

### 2.3 Main Logical Sections in runtime_probe.c

| Section | Lines | Role |
|---|---|---|
| Includes and cache struct | 1–16 | Module state: `g_cache` holds results after first probe |
| Helper functions | 18–44 | `is_executable_file`, `copy_string`, `starts_with` |
| Binary path resolution | 46–182 | Four-level search: env → bundled → PATH → fallback |
| GPU detection | 184–280 | VAAPI encoder probe via `/dev/dri` |
| Public API | 282–332 | Thin wrappers that call `linux_probe_codec_support()` |

---

## 3. Detailed Function-by-Function Analysis

### 3.1 Helper Functions (lines 18–44)

#### `is_executable_file()` — lines 18–21

```c
static int is_executable_file(const char *path)
{
    return path && path[0] != '\0' && access(path, X_OK) == 0;
}
```

**Purpose:** Returns 1 if `path` is a non-empty string pointing to a file for
which the current process has execute permission.

**Platform status:**

| Platform | Status | Notes |
|---|---|---|
| Linux | ✅ Native | `access(X_OK)` defined by POSIX |
| macOS | ✅ Native | Same POSIX `access()` semantics |
| Windows | ❌ Broken | `access()` exists as `_access()` in `<io.h>` but `X_OK` (= 1) only checks that the file exists, not that it is executable; the correct check for `.exe` files is `_access(path, 0)` combined with an `.exe` extension check or `GetBinaryType()` |

#### `copy_string()` — lines 23–33

```c
static void copy_string(char *dst, size_t dst_sz, const char *src)
```

**Purpose:** Safe bounded string copy. Handles `NULL` src by writing `""`.

**Platform status:** ✅ Fully portable — uses only standard C `strncpy`.

#### `starts_with()` — lines 35–44

```c
static int starts_with(const char *text, const char *prefix)
```

**Purpose:** Returns 1 if `text` begins with `prefix`.

**Platform status:** ✅ Fully portable — uses only standard C `strlen` /
`strncmp`.

---

### 3.2 Binary Path Resolution (lines 46–182)

#### `get_process_dir()` — lines 46–67

```c
static int get_process_dir(char *out_dir, size_t out_dir_sz)
{
    char exe_path[PATH_MAX];
    ssize_t len;
    char *last_slash;

    len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    ...
    last_slash = strrchr(exe_path, '/');
    ...
    *last_slash = '\0';
    copy_string(out_dir, out_dir_sz, exe_path);
    return 1;
}
```

**Purpose:** Determines the directory containing the running executable so that
bundled copies of tools can be found relative to it.

**Platform status:**

| Platform | Status | Replacement API |
|---|---|---|
| Linux | ✅ Native | `readlink("/proc/self/exe", ...)` |
| macOS | ❌ Broken | Must use `_NSGetExecutablePath()` from `<mach-o/dyld.h>` |
| Windows | ❌ Broken | Must use `GetModuleFileNameW(NULL, buf, MAX_PATH)` from `<windows.h>` |

**This is the most critical platform-specific function.** Binary resolution
fails entirely if `get_process_dir()` returns 0, because the bundled binary
check is skipped.

#### `try_bundled_candidate()` — lines 69–86

```c
static int try_bundled_candidate(const char *base_dir,
                                 const char *relative_path,
                                 const char *name,
                                 char *out_path,
                                 size_t out_path_sz)
```

**Purpose:** Builds `base_dir/relative_path/name` and checks whether it is
executable.

**Platform status:**

| Platform | Status | Notes |
|---|---|---|
| Linux | ✅ | `snprintf("%s/%s/%s", ...)` correct |
| macOS | ✅ | Same POSIX path separator |
| Windows | ⚠️ Partially | `/` separators work with Win32 APIs, but `snprintf` with `%s/%s` will produce forward-slash paths; these work for most operations but `is_executable_file()` must also be fixed |

#### `resolve_bundled_binary()` — lines 88–113

```c
static int resolve_bundled_binary(const char *name, char *out_path, size_t out_path_sz)
```

**Purpose:** Searches for a bundled binary in:
1. `<exe_dir>/<name>` (tool placed next to executable)
2. `<exe_dir>/bin/<name>` (standard `bin/` subdirectory)
3. `FFMPEG_CONVERTER_SOURCE_DIR/src/platform/<platform>/bin/<name>` (dev build only, where `<platform>` is `linux`, `macos`, or `windows`)

**Platform status:** The logic is portable once `get_process_dir()` and
`is_executable_file()` are fixed. However, item 3 currently uses a hard-coded
`src/platform/linux/bin` path; this must be generalised per platform
(e.g. `src/platform/windows/bin` on Windows, `src/platform/macos/bin` on macOS).

#### `resolve_path_binary()` — lines 115–146

```c
static int resolve_path_binary(const char *name, char *out_path, size_t out_path_sz)
{
    ...
    dir = strtok_r(path_copy, ":", &saveptr);   // ":" is POSIX PATH separator
    while (dir) {
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, name);
        ...
        dir = strtok_r(NULL, ":", &saveptr);
    }
    return 0;
}
```

**Purpose:** Iterates `$PATH` directories and checks for an executable named
`name`.

**Platform status:**

| Platform | Status | Notes |
|---|---|---|
| Linux | ✅ | PATH uses `:` separator, `/` path separator |
| macOS | ✅ | Same as Linux |
| Windows | ❌ Broken | `PATH` uses `;` separator; paths use `\` separator; binary names need `.exe` suffix; `strtok_r` is not available in MSVC (use `strtok_s`) |

#### `resolve_preferred_binary()` — lines 148–182

```c
static void resolve_preferred_binary(const char *env_name_primary,
                                     const char *env_name_secondary,
                                     const char *binary_name,
                                     char *out_path,
                                     size_t out_path_sz,
                                     int *using_bundled)
```

**Purpose:** Four-level priority chain for resolving a binary path:

```
Priority 1: env_name_primary  (e.g. $FFMPEG)
Priority 2: env_name_secondary (e.g. $FFMPEG_BIN)
Priority 3: bundled copy next to / near the executable
Priority 4: system PATH search
Fallback:   bare binary name (let the shell find it, or fail)
```

**Platform status:** The logic is fully portable; only the helpers it calls
(`is_executable_file`, `resolve_bundled_binary`, `resolve_path_binary`) need
platform-specific replacements.

---

### 3.3 GPU Detection (lines 184–280)

#### `probe_vaapi_encoder()` — lines 184–210

```c
static int probe_vaapi_encoder(const char *ffmpeg_bin,
                               const char *render_node,
                               const char *encoder_name)
{
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -v error -hide_banner "
             "-init_hw_device vaapi=va:\"%s\" "
             "-f lavfi -i color=size=1920x1080:rate=1 "
             "-frames:v 1 -vf format=nv12,hwupload "
             "-c:v %s -f null - >/dev/null 2>&1",
             ffmpeg_bin, render_node, encoder_name);
    rc = system(cmd);
    return WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
}
```

**Purpose:** Runs a 1-frame VAAPI test encode to verify that a specific
hardware encoder works on a given DRM render node.

**Platform status:**

| Platform | Status | Notes |
|---|---|---|
| Linux | ✅ Native | VAAPI + `/dev/dri/renderD*` is Linux-specific |
| macOS | ❌ N/A | macOS uses VideoToolbox (VT); no VAAPI; no `/dev/dri` |
| Windows | ❌ N/A | Windows uses NVENC/AMD AMF/Intel QSV; no VAAPI; no `/dev/dri` |

The entire function is Linux-only. Equivalent functions for other platforms
must use different encoder names and detection mechanisms.

#### `linux_probe_codec_support()` — lines 212–280

```c
int linux_probe_codec_support(LinuxCodecSupport *out_support)
{
    ...
    dir = opendir("/dev/dri");         // Linux-only
    while ((entry = readdir(dir))) {
        if (!starts_with(entry->d_name, "renderD"))
            continue;
        snprintf(render_node, ..., "/dev/dri/%s", entry->d_name);
        if (access(render_node, R_OK | W_OK) != 0)  // POSIX
            continue;
        has_h264 = probe_vaapi_encoder(..., "h264_vaapi");
        has_hevc = probe_vaapi_encoder(..., "hevc_vaapi");
        ...
    }
    closedir(dir);
    ...
}
```

**Purpose:** Orchestrates binary resolution + GPU detection, stores result in
module-level cache `g_cache`.

**Platform status:** The binary resolution calls are portable once helpers are
fixed. The `opendir("/dev/dri")` block is entirely Linux-specific and must be
replaced per platform:

- **macOS**: Query VideoToolbox encoder availability (no directory to scan).
- **Windows**: Query DXVA2/D3D11VA / NVENC / QSV availability (registry or
  ffmpeg encoder list).

---

### 3.4 Public API (lines 282–332)

All eight public functions are thin wrappers:

```c
int linux_is_bundled_ffmpeg_available(void)
{
    char path[PATH_MAX];
    return resolve_bundled_binary("ffmpeg", path, sizeof(path));
}

const char *linux_get_preferred_ffmpeg_bin(void)
{
    linux_probe_codec_support(NULL);
    return g_cache.support.ffmpeg_bin;
}
// ... (same pattern for ffprobe, mkvmerge, MP4Box)
```

**Platform status:** Logic is fully portable. The `linux_` prefix in the names
is the only issue — it must be changed to a platform-neutral prefix (or a
per-platform prefix matching the host) so that `converter.c` can call through
a common API without `#ifdef` at every call site.

---

## 4. Linux-Specific Code Identification

### 4.1 Critical POSIX Dependencies

| Code | Line | Issue | Windows | macOS |
|---|---|---|---|---|
| `readlink("/proc/self/exe", ...)` | 55 | Linux `/proc` filesystem | ❌ Does not exist | ❌ Must use `_NSGetExecutablePath()` |
| `opendir("/dev/dri")` | 242 | Linux DRM device directory | ❌ Does not exist | ❌ Does not exist |
| `entry->d_name` / `readdir()` | 244 | POSIX `<dirent.h>` | ⚠️ Available in MinGW/MSYS2, not MSVC | ✅ POSIX |
| `strtok_r(path_copy, ":", ...)` | 130 | PATH separator `":"` | ❌ Use `";"` + `strtok_s()` | ✅ Same `":"` |
| `access(path, X_OK)` | 20, 253 | POSIX executable check | ⚠️ `_access()` exists but `X_OK` semantics differ | ✅ Same `access()` |
| `access(render_node, R_OK \| W_OK)` | 253 | POSIX read/write check | ❌ Different semantics | ❌ N/A (no VAAPI) |
| `WIFEXITED(rc)` / `WEXITSTATUS(rc)` | 209 | POSIX wait macros from `<sys/wait.h>` | ❌ Not available on MSVC | ✅ POSIX |
| `ssize_t` | 49 | POSIX type from `<unistd.h>` | ❌ Not in MSVC | ✅ POSIX |
| `PATH_MAX` | 48, 75, 284 | POSIX constant from `<limits.h>` | ⚠️ Available as `MAX_PATH` (260) | ✅ `<limits.h>` |

### 4.2 Headers Analysis

```c
// Current includes in runtime_probe.c:
#include "runtime_probe.h"   // own header
#include <dirent.h>          // opendir/readdir/closedir — POSIX only
#include <limits.h>          // PATH_MAX — POSIX; on Windows use MAX_PATH
#include <stdio.h>           // snprintf — standard C ✅
#include <stdlib.h>          // getenv, system — standard C ✅
#include <string.h>          // strncpy, strrchr, strlen — standard C ✅
#include <sys/wait.h>        // WIFEXITED, WEXITSTATUS — POSIX only
#include <unistd.h>          // readlink, access, ssize_t — POSIX only
```

| Header | Linux | macOS | Windows (MSVC) | Windows (MinGW) |
|---|---|---|---|---|
| `<dirent.h>` | ✅ | ✅ | ❌ | ✅ (winpthreads) |
| `<limits.h>` | ✅ | ✅ | ⚠️ No `PATH_MAX` | ✅ |
| `<stdio.h>` | ✅ | ✅ | ✅ | ✅ |
| `<stdlib.h>` | ✅ | ✅ | ✅ | ✅ |
| `<string.h>` | ✅ | ✅ | ✅ | ✅ |
| `<sys/wait.h>` | ✅ | ✅ | ❌ | ❌ |
| `<unistd.h>` | ✅ | ✅ | ❌ | ⚠️ Partial |

### 4.3 Portability Classification by Function

| Function | Portable? | Effort to port |
|---|---|---|
| `is_executable_file()` | ⚠️ Partial | LOW — replace `access(X_OK)` on Windows |
| `copy_string()` | ✅ Yes | None |
| `starts_with()` | ✅ Yes | None |
| `get_process_dir()` | ❌ Linux only | HIGH — three separate OS APIs |
| `try_bundled_candidate()` | ⚠️ Partial | LOW — forward-slash paths work on Win32 |
| `resolve_bundled_binary()` | ⚠️ Partial | LOW — fix hardcoded `linux/bin` path |
| `resolve_path_binary()` | ⚠️ Partial | MEDIUM — PATH separator + `strtok_r` → `strtok_s` |
| `resolve_preferred_binary()` | ✅ Yes | None (depends only on helpers above) |
| `probe_vaapi_encoder()` | ❌ Linux only | HIGH — entirely replaced per platform |
| `linux_probe_codec_support()` | ❌ Linux only | HIGH — GPU block replaced per platform |
| Public API wrappers (×8) | ⚠️ Name only | LOW — rename prefix |

---

## 5. Required Windows Implementation

### 5.1 Executable Directory on Windows

Replace `get_process_dir()` with the following approach:

```c
// Windows implementation (windows_get_process_dir)
#include <windows.h>

static int windows_get_process_dir(char *out_dir, size_t out_dir_sz)
{
    wchar_t wide_path[MAX_PATH];
    char    utf8_path[MAX_PATH * 4];
    char   *last_sep;
    DWORD   len;

    len = GetModuleFileNameW(NULL, wide_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return 0;

    if (!WideCharToMultiByte(CP_UTF8, 0, wide_path, -1,
                             utf8_path, sizeof(utf8_path), NULL, NULL))
        return 0;

    /* Strip trailing filename — find last backslash or forward slash */
    last_sep = strrchr(utf8_path, '\\');
    if (!last_sep)
        last_sep = strrchr(utf8_path, '/');
    if (!last_sep)
        return 0;

    *last_sep = '\0';
    copy_string(out_dir, out_dir_sz, utf8_path);
    return 1;
}
```

### 5.2 PATH Search on Windows

Replace `resolve_path_binary()` with the following approach:

```c
// Windows implementation (windows_resolve_path_binary)
// Differences:
//   - PATH separator is ";" not ":"
//   - strtok_r → strtok_s (MSVC) or strtok_r (MinGW)
//   - binary name needs ".exe" appended if not already present
//   - path separator is "\" (but "/" works with Win32 APIs)

static int windows_is_executable_file(const char *path)
{
    /* On Windows, executability is implied by the .exe extension. */
    size_t len;
    DWORD  attrs;

    if (!path || path[0] == '\0')
        return 0;

    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return 0;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
        return 0;

    /* Accept path as-is if it ends with .exe, .bat, .cmd */
    len = strlen(path);
    if (len >= 4) {
        const char *ext = path + len - 4;
        if (_stricmp(ext, ".exe") == 0 ||
            _stricmp(ext, ".bat") == 0 ||
            _stricmp(ext, ".cmd") == 0)
            return 1;
    }
    return 0;
}

static int windows_resolve_path_binary(const char *name,
                                       char       *out_path,
                                       size_t      out_path_sz)
{
    const char *path_env;
    char        path_copy[8192];
    char        name_exe[MAX_PATH];
    char       *dir;
    char       *saveptr = NULL;

    /* Ensure name has .exe suffix */
    snprintf(name_exe, sizeof(name_exe), "%s", name);
    if (strlen(name_exe) < 4 ||
        _stricmp(name_exe + strlen(name_exe) - 4, ".exe") != 0) {
        strncat(name_exe, ".exe", sizeof(name_exe) - strlen(name_exe) - 1);
    }

    path_env = getenv("PATH");
    if (!path_env || path_env[0] == '\0')
        return 0;

    copy_string(path_copy, sizeof(path_copy), path_env);
    dir = strtok_s(path_copy, ";", &saveptr);  /* ";" on Windows */
    while (dir) {
        char candidate[MAX_PATH];

        if (dir[0] != '\0') {
            snprintf(candidate, sizeof(candidate), "%s\\%s", dir, name_exe);
            if (windows_is_executable_file(candidate)) {
                copy_string(out_path, out_path_sz, candidate);
                return 1;
            }
        }
        dir = strtok_s(NULL, ";", &saveptr);
    }
    return 0;
}
```

### 5.3 Executable File Check on Windows

As shown in Section 5.2, use `GetFileAttributesA()` + extension check. An
alternative that handles all executable types is `GetBinaryTypeA()`, but it
requires the file to actually exist and be a valid PE, which is exactly the
behavior we want.

### 5.4 GPU Detection on Windows

Windows uses **NVENC** (NVIDIA), **AMD AMF**, and **Intel QSV** for hardware
encoding. There is no VAAPI and no `/dev/dri`. The recommended detection method
is to run a short test encode with ffmpeg for each encoder name, similar to
`probe_vaapi_encoder()`:

```c
// Windows GPU codec names passed to -c:v:
//   NVIDIA NVENC  : h264_nvenc, hevc_nvenc
//   AMD AMF       : h264_amf,   hevc_amf
//   Intel QSV     : h264_qsv,   hevc_qsv

static int windows_probe_encoder(const char *ffmpeg_bin,
                                 const char *encoder_name)
{
    char cmd[8192];
    int  rc;

    if (!ffmpeg_bin || !encoder_name)
        return 0;

    /* Use "2>nul" instead of "2>/dev/null" on Windows */
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -v error -hide_banner "
             "-f lavfi -i color=size=1920x1080:rate=1 "
             "-frames:v 1 "
             "-c:v %s -f null - 2>nul",
             ffmpeg_bin, encoder_name);

    rc = _spawnl(_P_WAIT, "cmd.exe", "cmd.exe", "/c", cmd, NULL);
    /* Or simply: rc = system(cmd); */
    return rc == 0;
}
```

The Windows data structure (`WindowsCodecSupport`) tracks:

- `has_h264_nvenc`, `has_hevc_nvenc`
- `has_h264_amf`, `has_hevc_amf`
- `has_h264_qsv`, `has_hevc_qsv`

### 5.5 Windows Data Structure

```c
typedef struct {
    /* GPU encoders */
    int  has_h264_nvenc;
    int  has_hevc_nvenc;
    int  has_h264_amf;
    int  has_hevc_amf;
    int  has_h264_qsv;
    int  has_hevc_qsv;
    /* Binary paths */
    char ffmpeg_bin[1024];
    char ffprobe_bin[1024];
    char mkvmerge_bin[1024];
    char mp4box_bin[1024];
    int  using_bundled_ffmpeg;
    int  using_bundled_ffprobe;
    int  using_bundled_mkvmerge;
    int  using_bundled_mp4box;
} WindowsCodecSupport;
```

---

## 6. Required macOS Implementation

### 6.1 Executable Directory on macOS

Replace `get_process_dir()` with `_NSGetExecutablePath()`:

```c
// macOS implementation
#include <mach-o/dyld.h>
#include <limits.h>

static int macos_get_process_dir(char *out_dir, size_t out_dir_sz)
{
    char  exe_path[PATH_MAX];
    char  real_path[PATH_MAX];
    uint32_t size = sizeof(exe_path);
    char *last_slash;

    if (_NSGetExecutablePath(exe_path, &size) != 0)
        return 0;

    /* Resolve symlinks (important for Homebrew-installed binaries) */
    if (!realpath(exe_path, real_path))
        copy_string(real_path, sizeof(real_path), exe_path);

    last_slash = strrchr(real_path, '/');
    if (!last_slash)
        return 0;

    *last_slash = '\0';
    copy_string(out_dir, out_dir_sz, real_path);
    return 1;
}
```

### 6.2 PATH Search on macOS

`resolve_path_binary()` is **fully portable** on macOS — PATH separator is `:`
and path separator is `/`, identical to Linux. The `strtok_r` function is
available. **No changes required** for the PATH search logic.

### 6.3 Executable File Check on macOS

`is_executable_file()` using `access(path, X_OK)` is **fully portable** on
macOS. **No changes required.**

### 6.4 GPU Detection on macOS

macOS uses **VideoToolbox (VT)** for hardware encoding. There is no VAAPI and
no DRM render node directory to scan. Detection is done through ffmpeg, which
exposes `h264_videotoolbox` and `hevc_videotoolbox` encoder names:

```c
static int macos_probe_vt_encoder(const char *ffmpeg_bin,
                                  const char *encoder_name)
{
    char cmd[8192];
    int  rc;

    if (!ffmpeg_bin || !encoder_name)
        return 0;

    snprintf(cmd, sizeof(cmd),
             "\"%s\" -v error -hide_banner "
             "-f lavfi -i color=size=1920x1080:rate=1 "
             "-frames:v 1 "
             "-c:v %s -f null - >/dev/null 2>&1",
             ffmpeg_bin, encoder_name);

    rc = system(cmd);
    return WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
}
```

The macOS data structure tracks:

- `has_h264_videotoolbox`
- `has_hevc_videotoolbox`

### 6.5 macOS Data Structure

```c
typedef struct {
    /* GPU encoders */
    int  has_h264_videotoolbox;
    int  has_hevc_videotoolbox;
    /* Binary paths */
    char ffmpeg_bin[1024];
    char ffprobe_bin[1024];
    char mkvmerge_bin[1024];
    char mp4box_bin[1024];
    int  using_bundled_ffmpeg;
    int  using_bundled_ffprobe;
    int  using_bundled_mkvmerge;
    int  using_bundled_mp4box;
} MacosCodecSupport;
```

---

## 7. Proposed New File Structure

```
src/platform/
├── runtime_probe_common.h          (NEW) — common types, shared helpers
├── runtime_probe_common.c          (NEW) — copy_string, starts_with + portable resolve_preferred_binary
│
├── linux/
│   ├── runtime_probe.h             (MODIFIED) — keep LinuxCodecSupport + linux_* API
│   └── runtime_probe.c             (MODIFIED) — replace get_process_dir(), keep VAAPI block
│
├── macos/
│   ├── runtime_probe.h             (NEW) — MacosCodecSupport + macos_* API
│   └── runtime_probe.c             (NEW) — _NSGetExecutablePath + VideoToolbox probe
│
└── windows/
    ├── runtime_probe.h             (NEW) — WindowsCodecSupport + windows_* API
    └── runtime_probe.c             (NEW) — GetModuleFileNameW + NVENC/AMF/QSV probe
```

Alternatively, if a **single unified API** is preferred over per-platform names,
a thin dispatch header can be added:

```
src/platform/
├── runtime_probe.h         (NEW) — platform-neutral RuntimeProbeResult + probe_*() API
├── runtime_probe_common.c  (NEW) — shared helpers
├── linux/runtime_probe.c   (MODIFIED) — Linux implementation of probe_*()
├── macos/runtime_probe.c   (NEW)      — macOS implementation
└── windows/runtime_probe.c (NEW)      — Windows implementation
```

The unified-API approach is preferred because it removes all `#ifdef` from
`converter.c` and `main.c`.

---

## 8. Platform Abstraction Interface

### 8.1 Common Header: runtime_probe.h

A single platform-neutral header placed at `src/platform/runtime_probe.h`
(or included by all three platform headers with a common subset) should
declare:

```c
#ifndef RUNTIME_PROBE_H
#define RUNTIME_PROBE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque result type — platform-specific fields added via RuntimeProbeGpu */
typedef struct RuntimeProbeResult RuntimeProbeResult;

int         probe_runtime(RuntimeProbeResult **out_result);
void        probe_runtime_free(RuntimeProbeResult *result);

const char *probe_get_ffmpeg_bin(void);
const char *probe_get_ffprobe_bin(void);
const char *probe_get_mkvmerge_bin(void);
const char *probe_get_mp4box_bin(void);

int         probe_is_bundled_ffmpeg(void);
int         probe_is_bundled_ffprobe(void);
int         probe_is_bundled_mkvmerge(void);
int         probe_is_bundled_mp4box(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_PROBE_H */
```

Each platform then provides its own header extending this with GPU-specific
fields and its own `struct RuntimeProbeResult` definition.

### 8.2 Common Data Structure: RuntimeProbeResult

The binary-resolution fields are identical on all platforms. Only the GPU
fields differ. A practical approach without opaque types:

```c
/* In src/platform/runtime_probe_common.h */
typedef struct {
    /* Binary paths — same on all platforms */
    char ffmpeg_bin[1024];
    char ffprobe_bin[1024];
    char mkvmerge_bin[1024];
    char mp4box_bin[1024];
    int  using_bundled_ffmpeg;
    int  using_bundled_ffprobe;
    int  using_bundled_mkvmerge;
    int  using_bundled_mp4box;
} RuntimeProbeBinaries;
```

Each platform struct embeds `RuntimeProbeBinaries` as its first member so that
a pointer cast between `LinuxCodecSupport *` and `RuntimeProbeBinaries *` is
safe:

```c
/* Linux */
typedef struct {
    RuntimeProbeBinaries bins;      /* must be first */
    int  has_h264_vaapi;
    int  has_hevc_vaapi;
    char default_render_node[1024];
} LinuxCodecSupport;

/* macOS */
typedef struct {
    RuntimeProbeBinaries bins;      /* must be first */
    int  has_h264_videotoolbox;
    int  has_hevc_videotoolbox;
} MacosCodecSupport;

/* Windows */
typedef struct {
    RuntimeProbeBinaries bins;      /* must be first */
    int  has_h264_nvenc;
    int  has_hevc_nvenc;
    int  has_h264_amf;
    int  has_hevc_amf;
    int  has_h264_qsv;
    int  has_hevc_qsv;
} WindowsCodecSupport;
```

### 8.3 Portable Helper Functions

These functions are identical on all platforms and should be extracted to
`src/platform/runtime_probe_common.c`:

| Function | Status |
|---|---|
| `copy_string()` | Move as-is — pure C |
| `starts_with()` | Move as-is — pure C |
| `try_bundled_candidate()` | Move with minor path-separator abstraction |
| `resolve_preferred_binary()` | Move as-is — depends only on extracted helpers |

These functions remain platform-specific (one copy per platform file):

| Function | Per-platform reason |
|---|---|
| `is_executable_file()` | `access(X_OK)` vs `_access()` vs `GetFileAttributesA()` |
| `get_process_dir()` | `/proc/self/exe` vs `_NSGetExecutablePath` vs `GetModuleFileNameW` |
| `resolve_bundled_binary()` | Platform-specific dev-build binary directory path |
| `resolve_path_binary()` | PATH separator `":"` vs `";"`, `strtok_r` vs `strtok_s` |
| `probe_vaapi_encoder()` | VAAPI — Linux only |
| `macos_probe_vt_encoder()` | VideoToolbox — macOS only |
| `windows_probe_encoder()` | NVENC/AMF/QSV — Windows only |
| `<platform>_probe_codec_support()` | GPU scan loop differs per platform |

---

## 9. CMakeLists.txt Changes

The build system must select the correct `runtime_probe.c` at compile time.
Currently no explicit selection exists; only the Linux file is present.

### Proposed Changes

```cmake
# In CMakeLists.txt (existing pattern from src/platform/ handling)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(ffmpeg_converter PRIVATE
        src/platform/runtime_probe_common.c
        src/platform/linux/runtime_probe.c
    )
    target_include_directories(ffmpeg_converter PRIVATE
        src/platform/linux
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_sources(ffmpeg_converter PRIVATE
        src/platform/runtime_probe_common.c
        src/platform/macos/runtime_probe.c
    )
    target_include_directories(ffmpeg_converter PRIVATE
        src/platform/macos
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_sources(ffmpeg_converter PRIVATE
        src/platform/runtime_probe_common.c
        src/platform/windows/runtime_probe.c
    )
    target_include_directories(ffmpeg_converter PRIVATE
        src/platform/windows
    )
endif()
```

Additionally, the dev-build bundled binary path
(`src/platform/linux/bin` in the current code) must be updated to point to the
correct platform directory (`src/platform/<platform>/bin`), where `<platform>`
is `linux`, `macos`, or `windows`.
This is currently hard-coded in `resolve_bundled_binary()` as a
`FFMPEG_CONVERTER_SOURCE_DIR`-relative path; the CMake build should pass the
correct platform directory via a compile definition:

```cmake
if(FFMPEG_CONVERTER_SOURCE_DIR)
    target_compile_definitions(ffmpeg_converter PRIVATE
        FFMPEG_CONVERTER_SOURCE_DIR="${FFMPEG_CONVERTER_SOURCE_DIR}"
        FFMPEG_CONVERTER_PLATFORM_BIN_SUBDIR="src/platform/${PLATFORM_SUBDIR}/bin"
    )
endif()
```

---

## 10. Implementation Phases

### Phase 1 — Extract Common Helpers (no functional change, low risk)

1. Create `src/platform/runtime_probe_common.h` with `RuntimeProbeBinaries`
   struct and declarations for `copy_string()`, `starts_with()`,
   `try_bundled_candidate()`, and `resolve_preferred_binary()`.
2. Create `src/platform/runtime_probe_common.c` with the implementations of
   those four functions, copied verbatim from `runtime_probe.c`.
3. Update `src/platform/linux/runtime_probe.c` to `#include` the new common
   header and remove the now-duplicate function bodies.
4. Verify Linux build and all existing tests still pass.

### Phase 2 — macOS Runtime Probe (medium risk)

1. Create `src/platform/macos/runtime_probe.h` with `MacosCodecSupport` and
   `macos_probe_codec_support()` + `macos_*` API declarations.
2. Create `src/platform/macos/runtime_probe.c` with:
   - `macos_get_process_dir()` using `_NSGetExecutablePath()` + `realpath()`
   - `macos_is_executable_file()` using `access(X_OK)` (identical to Linux)
   - `macos_resolve_path_binary()` using `strtok_r` + `":"` (identical to Linux)
   - `macos_resolve_bundled_binary()` adapted for macOS `.app` bundle layout
   - `macos_probe_vt_encoder()` using VideoToolbox encoder names
   - `macos_probe_codec_support()` calling the above
3. Update `CMakeLists.txt` to include `runtime_probe_common.c` +
   `macos/runtime_probe.c` on `CMAKE_SYSTEM_NAME STREQUAL "Darwin"`.
4. Update call sites in `converter.c` and `src/cli/macos/main.c` (if present)
   to call `macos_probe_codec_support()` instead of `linux_probe_codec_support()`.
5. Verify macOS build and runtime probe output.

### Phase 3 — Windows Runtime Probe (high risk)

1. Create `src/platform/windows/runtime_probe.h` with `WindowsCodecSupport`
   and `windows_probe_codec_support()` + `windows_*` API declarations.
2. Create `src/platform/windows/runtime_probe.c` with:
   - `windows_get_process_dir()` using `GetModuleFileNameW()` + UTF-8 conversion
   - `windows_is_executable_file()` using `GetFileAttributesA()` + extension check
   - `windows_resolve_path_binary()` using `strtok_s` + `";"` separator + `.exe`
   - `windows_resolve_bundled_binary()` adapted for Windows install layout
   - `windows_probe_encoder()` using NVENC/AMF/QSV encoder names + `2>nul`
   - `windows_probe_codec_support()` probing all three GPU vendor stacks
3. Update `CMakeLists.txt` to include `runtime_probe_common.c` +
   `windows/runtime_probe.c` on `CMAKE_SYSTEM_NAME STREQUAL "Windows"`.
4. Update call sites in `converter.c` to call `windows_probe_codec_support()`
   under `#ifdef _WIN32`.
5. Remove the `#if defined(__linux__)` guard that currently suppresses all
   runtime probe calls on non-Linux builds.
6. Verify Windows build (MSYS2/MinGW or MSVC) and runtime probe output.

### Phase 4 — Unified API (optional, reduces #ifdef at call sites)

1. Create `src/platform/runtime_probe.h` with platform-neutral typedefs and
   a uniform `probe_*()` function API.
2. Make each platform header and implementation satisfy this interface.
3. Remove all remaining `#if defined(__linux__)` guards from `converter.c` and
   `main.c`; replace with calls through the unified API.
4. Update `CMakeLists.txt` include paths to expose only the unified header.

---

## 11. Risk Assessment

| Risk | Probability | Impact | Mitigation |
|---|---|---|---|
| `GetModuleFileNameW` returns path to a DLL, not the exe | LOW | HIGH | Pass `NULL` as first argument — returns the exe path |
| `.exe` extension check misses `ffmpeg` without extension | MEDIUM | MEDIUM | Also try appending `.exe` if first attempt fails |
| macOS `.app` bundle changes exe location | MEDIUM | HIGH | Use `realpath()` to resolve symlinks; check both `Contents/MacOS/` and sibling `bin/` |
| VideoToolbox not available on older macOS | LOW | LOW | Probe fails gracefully; software fallback is used |
| NVENC/AMF/QSV probe takes long on slow systems | MEDIUM | LOW | Probe runs once and is cached; run in background thread if startup time is critical |
| `strtok_s` not available in older MSVC | LOW | MEDIUM | Use custom thread-safe tokeniser if targeting pre-VS2005 |
| `system()` blocked by security software on Windows | LOW | HIGH | Use `CreateProcessW()` instead of `system()` for all probe commands |
| Bundled binary path differs per packaging tool | HIGH | MEDIUM | Make `FFMPEG_CONVERTER_SOURCE_DIR` and `FFMPEG_CONVERTER_PLATFORM_BIN_SUBDIR` configurable at CMake configure time |
| Race condition in `g_cache.initialized` check | LOW | LOW | Add `pthread_once` (POSIX) / `InitOnceExecuteOnce` (Windows) guard if multi-threaded probe is needed |
