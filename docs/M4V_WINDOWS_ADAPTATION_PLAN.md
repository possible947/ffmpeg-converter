# M4V_WINDOWS_ADAPTATION_PLAN.md

Detailed plan for adapting `src/m4v/m4v.c` to support Windows while preserving
full functionality on Linux (and compatible behaviour on macOS).

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Quick Reference — Issues to Fix](#2-quick-reference--issues-to-fix)
3. [Architecture Overview](#3-architecture-overview)
   - 3.1 [Current Linux-Only Structure](#31-current-linux-only-structure)
   - 3.2 [Proposed Cross-Platform Structure](#32-proposed-cross-platform-structure)
4. [Detailed Implementation Plan](#4-detailed-implementation-plan)
   - 4.1 [Phase 1: Platform Abstraction Layer (8–12 hours)](#41-phase-1-platform-abstraction-layer-812-hours)
   - 4.2 [Phase 2: Windows Platform Implementation (8–14 hours)](#42-phase-2-windows-platform-implementation-814-hours)
   - 4.3 [Phase 3: Refactor m4v.c (8–14 hours)](#43-phase-3-refactor-m4vc-814-hours)
5. [CMakeLists.txt Changes](#5-cmakeliststxt-changes)
6. [Risk Assessment](#6-risk-assessment)

---

## 1. Executive Summary

### Status Overview

| Item | Status |
|------|--------|
| M4V needed on Windows CLI | ✅ Confirmed |
| Analysis completed (PR #16) | ✅ Done — see `docs/M4V_DETAILED_ANALYSIS.md` |
| Critical blockers identified | 🚨 7 CRITICAL blockers |
| Full porting plan created | ✅ This document |
| Effort estimate | 📊 26–43 working hours |
| Scope | 🎯 Platform abstraction layer + Windows implementations |

### Platform Support Today

| Platform | Compile | Run | Goal |
|----------|---------|-----|------|
| Linux | ✅ | ✅ Works | ✅ Keep working |
| macOS (GUI) | ❌ Does not compile | ❌ — | ⬛ Out of scope |
| macOS (CLI) | ❌ Does not compile | ❌ — | ⬛ Out of scope |
| Windows CLI | ❌ Does not compile | ❌ — | 🎯 **This plan** |

> **Scope note:** This plan targets **Windows CLI** support exclusively.
> macOS support (both GUI and CLI) is out of scope for this adaptation work.
> The POSIX platform layer created in Phase 1 is reusable for a future macOS
> port, but macOS-specific binary resolution and testing are not covered here.

### Summary of Blockers

`src/m4v/m4v.c` (644 lines) contains **seven categories** of hard-wired
Linux/POSIX dependencies. Three are immediate compilation blockers (undefined
symbols), four are runtime blockers (wrong OS API). Every blocker must be
resolved before the module can build and run on Windows.

---

## 2. Quick Reference — Issues to Fix

| # | Issue | Line | Severity | Solution | File | Effort |
|---|-------|------|----------|----------|------|--------|
| 1 | Unconditional `#include "linux/runtime_probe.h"` | 13 | 🔴 BLOCKER | Add `#if defined(__linux__)` guards | `m4v.c` | 0.5 h |
| 2 | `linux_get_preferred_ffprobe_bin()` call | 400 | 🔴 BLOCKER | Create `m4v_platform` abstraction | `m4v_platform.h` | 2 h |
| 3 | `linux_get_preferred_ffmpeg_bin()` call | 492 | 🔴 BLOCKER | Use abstraction layer | `m4v_platform.h` | — |
| 4 | `linux_get_preferred_ffprobe_bin()` call | 493 | 🔴 BLOCKER | Use abstraction layer | `m4v_platform.h` | — |
| 5 | `linux_get_preferred_mp4box_bin()` call | 494 | 🔴 BLOCKER | Use abstraction layer | `m4v_platform.h` | — |
| 6 | Hardcoded `/tmp/` directory | 317 | 🔴 BLOCKER | `m4v_platform_make_temp_dir()` | `m4v_platform.c` | 3–4 h |
| 7 | `mkdtemp()` POSIX only | 318 | 🔴 BLOCKER | Windows: `GetTempPath` + `CreateDirectory` | `m4v_platform_windows.c` | 2 h |
| 8 | Shell `rm -rf` command | 335 | 🔴 BLOCKER | Recursive directory API | `m4v_platform_windows.c` | 3–4 h |
| + | Shell quoting (single vs double quotes) | 73–85 | 🟡 ADAPT | Platform-specific quoting | `m4v_platform.c` | 1 h |
| + | `WIFEXITED` macro | 154 | 🟡 ADAPT | `pclose_exit_code()` wrapper | `m4v_platform.c` | 0.5 h |
| + | Path separator `'/'` | 366, 374 | 🟡 LOW | Optional: add path abstraction | `m4v.c` | 1–2 h |

> **Note on issues 3–5:** these three calls are all resolved by the same
> abstraction created for issue 2; they are listed separately because they are
> in a different function (`m4v_create_from_input()` at lines 492–494) and
> block the entire five-step pipeline.

---

## 3. Architecture Overview

### 3.1 Current Linux-Only Structure

```
src/m4v/
├── m4v.h          (43 lines  — public API header)
├── m4v.c          (644 lines — full implementation, Linux-only)
└── CMakeLists.txt
```

`m4v.c` internals:

```
m4v.c (644 lines)
├── Direct calls to linux_get_preferred_ffmpeg_bin()
├── Direct calls to linux_get_preferred_ffprobe_bin()
├── Direct calls to linux_get_preferred_mp4box_bin()
├── Hardcoded /tmp/ path (line 317)
├── POSIX mkdtemp() (line 318)
├── Shell rm -rf command via system() (lines 335–336)
├── POSIX popen/pclose/WIFEXITED/WEXITSTATUS
├── POSIX stat/access/unlink
└── Single-quote shell quoting
```

### 3.2 Proposed Cross-Platform Structure

```
src/m4v/
├── m4v.h                              (unchanged — public API)
├── m4v.c                              (refactored ~650 lines — platform-agnostic)
│   ├── Platform-agnostic business logic
│   ├── Calls to m4v_platform_* abstraction
│   └── #include "m4v_platform.h"
├── m4v_platform.h                     (NEW — 50 lines, 11 wrapper signatures)
├── CMakeLists.txt                     (updated)
└── platform/
    ├── m4v_platform_posix.c           (NEW — ~200 lines, Linux + macOS)
    │   ├── make_temp_dir()  via mkdtemp()
    │   ├── remove_temp_dir() via rm -rf (or nftw())
    │   ├── get_ffmpeg_bin() via linux_get_preferred_ffmpeg_bin()
    │   ├── get_ffprobe_bin() / get_mp4box_bin() — same pattern
    │   ├── File ops: stat, unlink, access
    │   ├── popen/pclose wrappers
    │   └── POSIX shell_quote()
    └── m4v_platform_windows.c         (NEW — ~250 lines, Windows)
        ├── make_temp_dir()  via GetTempPath/GetTempFileName/CreateDirectory
        ├── remove_temp_dir() via FindFirstFile recursive loop
        ├── get_*_bin() via env vars → bundled → known paths → PATH
        ├── File ops: GetFileAttributes, DeleteFile
        ├── _popen/_pclose wrappers
        └── Double-quote shell_quote() for cmd.exe
```

Additionally, a new Windows binary resolver is needed under the platform
directory:

```
src/platform/windows/
├── m4v_windows_probe.h                (NEW — 30 lines)
└── m4v_windows_probe.c                (NEW — 150 lines)
```

---

## 4. Detailed Implementation Plan

### 4.1 Phase 1: Platform Abstraction Layer (8–12 hours)

**Goal:** Create the abstraction interface and the POSIX implementation so that
the Linux build continues to work without any change to its observable
behaviour. All subsequent work (Phase 2 and 3) depends on this layer.

**Files to create:**

| File | Lines | Purpose |
|------|-------|---------|
| `src/m4v/m4v_platform.h` | ~50 | Common interface (11 signatures) |
| `src/m4v/platform/m4v_platform_posix.c` | ~200 | POSIX/Linux implementation |

#### Task 1.1 — Create `m4v_platform.h`

Define the 11 wrapper function signatures:

```c
/* src/m4v/m4v_platform.h */
#ifndef M4V_PLATFORM_H
#define M4V_PLATFORM_H

#include <stddef.h>
#include <stdio.h>

/* PATH_MAX fallback (MSVC does not define it) */
#ifndef PATH_MAX
#  define PATH_MAX 4096
#endif

/* strtok_r / strtok_s compatibility.
 * Both strtok_r (POSIX) and strtok_s (MSVC) share the same three-argument
 * signature: (char *str, const char *delim, char **context).  A simple
 * define alias is sufficient; no parameter reordering is needed.
 */
#if defined(_MSC_VER)
#  define strtok_r strtok_s
#endif

/* Temp directory management */
int  m4v_platform_make_temp_dir(char *path, size_t path_sz);
void m4v_platform_remove_temp_dir(const char *dir);

/* Binary resolution */
const char *m4v_platform_get_ffmpeg_bin(void);
const char *m4v_platform_get_ffprobe_bin(void);
const char *m4v_platform_get_mp4box_bin(void);

/* File operations */
int  m4v_platform_is_regular_file(const char *path);
int  m4v_platform_unlink(const char *path);

/* Process execution */
FILE *m4v_platform_popen(const char *cmd, const char *mode);
int   m4v_platform_pclose_exit_code(FILE *fp);

/* Shell quoting */
void m4v_platform_shell_quote(const char *input, char *out, size_t out_sz);

/* Shell redirect for stderr suppression */
const char *m4v_platform_null_redirect(void);

#endif /* M4V_PLATFORM_H */
```

#### Task 1.2 — Implement POSIX versions in `m4v_platform_posix.c`

```c
/* src/m4v/platform/m4v_platform_posix.c */
#include "../m4v_platform.h"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(__linux__)
#  include "../../platform/linux/runtime_probe.h"
#endif

/* ---- Temp directory ---------------------------------------------------- */

int m4v_platform_make_temp_dir(char *path, size_t path_sz)
{
    char templ[1024];
    char *made;
    const char *tmpdir;

    if (!path || path_sz == 0)
        return 0;

    tmpdir = getenv("TMPDIR");
    if (!tmpdir || tmpdir[0] == '\0')
        tmpdir = "/tmp";

    snprintf(templ, sizeof(templ), "%s/m4v_mux_XXXXXX", tmpdir);
    made = mkdtemp(templ);
    if (!made)
        return 0;

    strncpy(path, made, path_sz - 1);
    path[path_sz - 1] = '\0';
    return 1;
}

void m4v_platform_remove_temp_dir(const char *dir)
{
    char cmd[2048];
    char quoted[1536];
    size_t pos = 0;

    if (!dir || dir[0] == '\0')
        return;

    /* POSIX single-quote the directory path */
    quoted[pos++] = '\'';
    while (*dir && pos + 5 < sizeof(quoted)) {
        if (*dir == '\'') {
            quoted[pos++] = '\'';
            quoted[pos++] = '\\';
            quoted[pos++] = '\'';
            quoted[pos++] = '\'';
        } else {
            quoted[pos++] = *dir;
        }
        dir++;
    }
    quoted[pos++] = '\'';
    quoted[pos]   = '\0';

    snprintf(cmd, sizeof(cmd), "rm -rf %s", quoted);
    system(cmd);
}

/* ---- Binary resolution -------------------------------------------------- */

const char *m4v_platform_get_ffmpeg_bin(void)
{
#if defined(__linux__)
    return linux_get_preferred_ffmpeg_bin();
#else
    return "ffmpeg";
#endif
}

const char *m4v_platform_get_ffprobe_bin(void)
{
#if defined(__linux__)
    return linux_get_preferred_ffprobe_bin();
#else
    return "ffprobe";
#endif
}

const char *m4v_platform_get_mp4box_bin(void)
{
#if defined(__linux__)
    return linux_get_preferred_mp4box_bin();
#else
    return "MP4Box";
#endif
}

/* ---- File operations ---------------------------------------------------- */

int m4v_platform_is_regular_file(const char *path)
{
    struct stat st;
    return path &&
           path[0] != '\0' &&
           stat(path, &st) == 0 &&
           S_ISREG(st.st_mode) &&
           access(path, R_OK) == 0;
}

int m4v_platform_unlink(const char *path)
{
    return unlink(path);
}

/* ---- Process execution -------------------------------------------------- */

FILE *m4v_platform_popen(const char *cmd, const char *mode)
{
    return popen(cmd, mode);
}

int m4v_platform_pclose_exit_code(FILE *fp)
{
    int rc = pclose(fp);
    if (rc == -1)
        return -1;
    if (WIFEXITED(rc))
        return WEXITSTATUS(rc);
    return -1;
}

/* ---- Shell quoting ------------------------------------------------------ */

void m4v_platform_shell_quote(const char *input, char *out, size_t out_sz)
{
    size_t pos = 0;

    if (!out || out_sz < 3) {
        if (out && out_sz) out[0] = '\0';
        return;
    }
    if (!input) input = "";

    out[pos++] = '\'';
    while (*input && pos + 5 < out_sz) {
        if (*input == '\'') {
            out[pos++] = '\'';
            out[pos++] = '\\';
            out[pos++] = '\'';
            out[pos++] = '\'';
        } else {
            out[pos++] = *input;
        }
        ++input;
    }
    out[pos++] = '\'';
    out[pos]   = '\0';
}

/* ---- Shell redirect ----------------------------------------------------- */

const char *m4v_platform_null_redirect(void)
{
    return "2>/dev/null";
}
```

#### Task 1.3 — Verify Linux build

After creating the two files above, verify that:

- The Linux build compiles without warnings.
- All existing m4v-related tests pass.
- No observable behaviour change occurs on Linux.

**Effort: 8–12 hours**

| Sub-task | Hours |
|----------|-------|
| Design & headers | 2 h |
| POSIX implementation | 4–6 h |
| Testing & fixes | 2–4 h |

---

### 4.2 Phase 2: Windows Platform Implementation (8–14 hours)

**Goal:** Create Windows-specific implementations for all platform wrappers and
a Windows binary resolver for ffmpeg, ffprobe, and MP4Box.

**Files to create:**

| File | Lines | Purpose |
|------|-------|---------|
| `src/m4v/platform/m4v_platform_windows.c` | ~250–300 | Win32-backed implementations |
| `src/platform/windows/m4v_windows_probe.h` | ~30 | Windows binary resolver API |
| `src/platform/windows/m4v_windows_probe.c` | ~150 | Windows binary resolver impl |

#### Task 2.1 — Windows temp directory (2–3 hours)

`GetTempPathA` returns the system temp directory; `GetTempFileNameA` generates a
unique name without a collision risk. The file is immediately deleted and the
name is reused as a directory:

```c
/* src/m4v/platform/m4v_platform_windows.c */
#include <windows.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include "../m4v_platform.h"

int m4v_platform_make_temp_dir(char *path, size_t path_sz)
{
    char base[MAX_PATH];
    char unique[MAX_PATH];

    if (!path || path_sz == 0)
        return 0;

    if (!GetTempPathA(sizeof(base), base))
        return 0;

    /* GetTempFileNameA creates a temp *file* — delete it and reuse the name */
    if (!GetTempFileNameA(base, "m4v", 0, unique))
        return 0;

    DeleteFileA(unique);  /* remove temp file, keep the unique name */

    if (!CreateDirectoryA(unique, NULL))
        return 0;

    strncpy(path, unique, path_sz - 1);
    path[path_sz - 1] = '\0';
    return 1;
}
```

#### Task 2.2 — Recursive directory removal (3–4 hours)

Windows has no `rm -rf` equivalent. The required implementation uses
`FindFirstFileA` / `FindNextFileA` to enumerate entries and recursively deletes
them before calling `RemoveDirectoryA` on the now-empty directory:

```c
static void remove_dir_recursive(const char *dir)
{
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char child[MAX_PATH];

    if (!dir || dir[0] == '\0')
        return;

    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 ||
                strcmp(fd.cFileName, "..") == 0)
                continue;

            snprintf(child, sizeof(child), "%s\\%s", dir, fd.cFileName);

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                remove_dir_recursive(child);
            else
                DeleteFileA(child);
        } while (FindNextFileA(h, &fd));

        FindClose(h);
    }

    RemoveDirectoryA(dir);
}

void m4v_platform_remove_temp_dir(const char *dir)
{
    if (dir && dir[0] != '\0')
        remove_dir_recursive(dir);
}
```

#### Task 2.3 — Windows binary resolution (3–4 hours)

Create `src/platform/windows/m4v_windows_probe.h`:

```c
/* src/platform/windows/m4v_windows_probe.h */
#ifndef M4V_WINDOWS_PROBE_H
#define M4V_WINDOWS_PROBE_H

const char *m4v_windows_get_ffmpeg_bin(void);
const char *m4v_windows_get_ffprobe_bin(void);
const char *m4v_windows_get_mp4box_bin(void);

#endif /* M4V_WINDOWS_PROBE_H */
```

Create `src/platform/windows/m4v_windows_probe.c` — the three functions share
the same search strategy. The ffmpeg resolver is shown in full; ffprobe and
MP4Box follow the same pattern with tool-specific names and candidate paths:

```c
/* src/platform/windows/m4v_windows_probe.c */
#include "m4v_windows_probe.h"

#include <windows.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int win_exe_exists(const char *path)
{
    return _access(path, 0) == 0;
}

/*
 * NOTE — thread safety: `resolved` is a function-local static buffer.
 * These functions are intended to be called once during initialisation.
 * Calling them concurrently from multiple threads is not supported.
 */
const char *m4v_windows_get_ffmpeg_bin(void)
{
    static char resolved[MAX_PATH];
    const char *env;
    char exe_dir[MAX_PATH];
    char *last_sep;
    const char *candidates[] = {
        "C:\\msys64\\mingw64\\bin\\ffmpeg.exe",
        "C:\\msys64\\usr\\bin\\ffmpeg.exe",
        "C:\\ProgramData\\chocolatey\\bin\\ffmpeg.exe",
        "C:\\tools\\ffmpeg\\bin\\ffmpeg.exe"
    };
    size_t i;
    const char *path_env;
    char path_copy[32768];
    char *ctx = NULL;
    char *dir;

    /* Priority 1: explicit env vars */
    env = getenv("FFMPEG");
    if (env && env[0] != '\0' && win_exe_exists(env)) return env;
    env = getenv("FFMPEG_BIN");
    if (env && env[0] != '\0' && win_exe_exists(env)) return env;

    /* Priority 2: bundled next to executable */
    if (GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir))) {
        last_sep = strrchr(exe_dir, '\\');
        if (!last_sep) last_sep = strrchr(exe_dir, '/');
        if (last_sep) {
            *last_sep = '\0';
            snprintf(resolved, sizeof(resolved), "%s\\bin\\ffmpeg.exe", exe_dir);
            if (win_exe_exists(resolved)) return resolved;
            snprintf(resolved, sizeof(resolved), "%s\\ffmpeg.exe", exe_dir);
            if (win_exe_exists(resolved)) return resolved;
        }
    }

    /* Priority 3: known package-manager locations */
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
        if (win_exe_exists(candidates[i])) return candidates[i];

    /* Priority 4: system PATH search */
    path_env = getenv("PATH");
    if (path_env && path_env[0] != '\0') {
        strncpy(path_copy, path_env, sizeof(path_copy) - 1);
        path_copy[sizeof(path_copy) - 1] = '\0';
        dir = strtok_s(path_copy, ";", &ctx);
        while (dir) {
            if (dir[0] != '\0') {
                snprintf(resolved, sizeof(resolved), "%s\\ffmpeg.exe", dir);
                if (win_exe_exists(resolved)) return resolved;
            }
            dir = strtok_s(NULL, ";", &ctx);
        }
    }

    /* Fallback: rely on PATH at runtime */
    return "ffmpeg.exe";
}

const char *m4v_windows_get_ffprobe_bin(void)
{
    static char resolved[MAX_PATH];
    const char *env;
    char exe_dir[MAX_PATH];
    char *last_sep;
    const char *candidates[] = {
        "C:\\msys64\\mingw64\\bin\\ffprobe.exe",
        "C:\\msys64\\usr\\bin\\ffprobe.exe",
        "C:\\ProgramData\\chocolatey\\bin\\ffprobe.exe",
        "C:\\tools\\ffmpeg\\bin\\ffprobe.exe"
    };
    size_t i;
    const char *path_env;
    char path_copy[32768];
    char *ctx = NULL;
    char *dir;

    env = getenv("FFPROBE");
    if (env && env[0] != '\0' && win_exe_exists(env)) return env;
    env = getenv("FFPROBE_BIN");
    if (env && env[0] != '\0' && win_exe_exists(env)) return env;

    if (GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir))) {
        last_sep = strrchr(exe_dir, '\\');
        if (!last_sep) last_sep = strrchr(exe_dir, '/');
        if (last_sep) {
            *last_sep = '\0';
            snprintf(resolved, sizeof(resolved), "%s\\bin\\ffprobe.exe", exe_dir);
            if (win_exe_exists(resolved)) return resolved;
            snprintf(resolved, sizeof(resolved), "%s\\ffprobe.exe", exe_dir);
            if (win_exe_exists(resolved)) return resolved;
        }
    }

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
        if (win_exe_exists(candidates[i])) return candidates[i];

    path_env = getenv("PATH");
    if (path_env && path_env[0] != '\0') {
        strncpy(path_copy, path_env, sizeof(path_copy) - 1);
        path_copy[sizeof(path_copy) - 1] = '\0';
        dir = strtok_s(path_copy, ";", &ctx);
        while (dir) {
            if (dir[0] != '\0') {
                snprintf(resolved, sizeof(resolved), "%s\\ffprobe.exe", dir);
                if (win_exe_exists(resolved)) return resolved;
            }
            dir = strtok_s(NULL, ";", &ctx);
        }
    }

    return "ffprobe.exe";
}

const char *m4v_windows_get_mp4box_bin(void)
{
    static char resolved[MAX_PATH];
    const char *env;
    char exe_dir[MAX_PATH];
    char *last_sep;
    const char *candidates[] = {
        "C:\\Program Files\\GPAC\\MP4Box.exe",
        "C:\\Program Files (x86)\\GPAC\\MP4Box.exe",
        "C:\\msys64\\mingw64\\bin\\MP4Box.exe",
        "C:\\msys64\\usr\\bin\\MP4Box.exe",
        "C:\\ProgramData\\chocolatey\\bin\\MP4Box.exe"
    };
    size_t i;
    const char *path_env;
    char path_copy[32768];
    char *ctx = NULL;
    char *dir;

    env = getenv("MP4BOX_BIN");
    if (env && env[0] != '\0' && win_exe_exists(env)) return env;
    env = getenv("MP4BOX");
    if (env && env[0] != '\0' && win_exe_exists(env)) return env;

    if (GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir))) {
        last_sep = strrchr(exe_dir, '\\');
        if (!last_sep) last_sep = strrchr(exe_dir, '/');
        if (last_sep) {
            *last_sep = '\0';
            snprintf(resolved, sizeof(resolved), "%s\\bin\\MP4Box.exe", exe_dir);
            if (win_exe_exists(resolved)) return resolved;
            snprintf(resolved, sizeof(resolved), "%s\\MP4Box.exe", exe_dir);
            if (win_exe_exists(resolved)) return resolved;
        }
    }

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
        if (win_exe_exists(candidates[i])) return candidates[i];

    path_env = getenv("PATH");
    if (path_env && path_env[0] != '\0') {
        strncpy(path_copy, path_env, sizeof(path_copy) - 1);
        path_copy[sizeof(path_copy) - 1] = '\0';
        dir = strtok_s(path_copy, ";", &ctx);
        while (dir) {
            if (dir[0] != '\0') {
                snprintf(resolved, sizeof(resolved), "%s\\MP4Box.exe", dir);
                if (win_exe_exists(resolved)) return resolved;
                snprintf(resolved, sizeof(resolved), "%s\\mp4box.exe", dir);
                if (win_exe_exists(resolved)) return resolved;
            }
            dir = strtok_s(NULL, ";", &ctx);
        }
    }

    return "MP4Box.exe";
}
```

Wire the Windows resolver into `m4v_platform_windows.c`:

```c
/* In src/m4v/platform/m4v_platform_windows.c */
#include "../../platform/windows/m4v_windows_probe.h"

const char *m4v_platform_get_ffmpeg_bin(void)  { return m4v_windows_get_ffmpeg_bin(); }
const char *m4v_platform_get_ffprobe_bin(void) { return m4v_windows_get_ffprobe_bin(); }
const char *m4v_platform_get_mp4box_bin(void)  { return m4v_windows_get_mp4box_bin(); }
```

#### Task 2.4 — File operations (1–2 hours)

```c
/* src/m4v/platform/m4v_platform_windows.c */

int m4v_platform_is_regular_file(const char *path)
{
    DWORD attr;
    if (!path || path[0] == '\0') return 0;
    attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES) &&
           !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

int m4v_platform_unlink(const char *path)
{
    return DeleteFileA(path) ? 0 : -1;
}
```

#### Task 2.5 — Command execution wrappers (1–2 hours)

```c
/* src/m4v/platform/m4v_platform_windows.c */

FILE *m4v_platform_popen(const char *cmd, const char *mode)
{
    return _popen(cmd, mode);
}

int m4v_platform_pclose_exit_code(FILE *fp)
{
    /*
     * On Windows _pclose() returns the exit code directly — no WIFEXITED
     * encoding.  Return -1 on error (same contract as POSIX implementation).
     */
    return _pclose(fp);
}

const char *m4v_platform_null_redirect(void)
{
    return "2>nul";
}
```

#### Task 2.6 — Shell quoting for cmd.exe (1 hour)

`cmd.exe` does not understand POSIX single-quote quoting. The safe approach is
double-quote wrapping with embedded `"` escaped as `""`:

```c
/* src/m4v/platform/m4v_platform_windows.c */

void m4v_platform_shell_quote(const char *input, char *out, size_t out_sz)
{
    size_t pos = 0;

    if (!out || out_sz < 3) {
        if (out && out_sz) out[0] = '\0';
        return;
    }
    if (!input) input = "";

    out[pos++] = '"';
    while (*input && pos + 4 < out_sz) {
        if (*input == '"') {
            out[pos++] = '"';
            out[pos++] = '"';   /* escape " as "" for cmd.exe */
        } else {
            out[pos++] = *input;
        }
        ++input;
    }
    out[pos++] = '"';
    out[pos]   = '\0';
}
```

> **Limitation:** This quoting strategy is adequate for file paths (the sole
> use case in `m4v.c`). It does **not** escape cmd.exe metacharacters such as
> `%`, `^`, `&`, `|`, `<`, or `>`. If user-supplied strings that may contain
> such characters are ever passed through this function, a more complete
> escaping strategy (e.g., prefixing each metacharacter with `^`) will be
> needed. For the current M4V pipeline all quoted strings are file system
> paths derived from user-supplied input file names where these characters are
> pathological but exceedingly rare in practice.

**Effort: 8–14 hours**

| Sub-task | Hours |
|----------|-------|
| Windows temp dir | 2–3 h |
| Recursive directory removal | 3–4 h |
| Windows binary resolution | 3–4 h |
| File operations | 1–2 h |
| Command execution wrappers | 1–2 h |
| Shell quoting | 1 h |

---

### 4.3 Phase 3: Refactor m4v.c (8–14 hours)

**Goal:** Replace all platform-specific code in `m4v.c` with calls to the
abstraction layer created in Phase 1 and Phase 2. The business logic (the
five-step M4V pipeline) must remain byte-for-byte identical.

#### Task 3.1 — Replace includes (0.5 hours)

```c
/* OLD — current m4v.c lines 1–13 */
#include "m4v.h"

#include <jansson.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "linux/runtime_probe.h"

/* NEW — portable includes only */
#include "m4v.h"
#include "m4v_platform.h"

#include <jansson.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
```

#### Task 3.2 — Replace `is_regular_file()` (0.5 hours)

```c
/* OLD — lines 45–54 */
static int is_regular_file(const char *path)
{
    struct stat st;
    return path &&
           path[0] != '\0' &&
           stat(path, &st) == 0 &&
           S_ISREG(st.st_mode) &&
           access(path, R_OK) == 0;
}

/* NEW — delegate to platform layer */
static int is_regular_file(const char *path)
{
    return m4v_platform_is_regular_file(path);
}
```

#### Task 3.3 — Replace `shell_quote()` (0.5 hours)

The primary `shell_quote()` function (POSIX single-quote) is platform-specific.
Replace the body with a call to `m4v_platform_shell_quote()`:

```c
/* OLD — lines 56–85 */
static void shell_quote(const char *input, char *out, size_t out_sz) { /* ... */ }

/* NEW */
static void shell_quote(const char *input, char *out, size_t out_sz)
{
    m4v_platform_shell_quote(input, out, out_sz);
}
```

> `shell_quote_double()` (lines 87–111) uses standard double-quote escaping and
> is already portable — leave it unchanged.

#### Task 3.4 — Replace `run_command_capture()` popen/pclose (1 hour)

```c
/* OLD — lines 113–157 */
fp = popen(cmd, "r");
...
status = pclose(fp);
if (WIFEXITED(status))
    return WEXITSTATUS(status);
return -1;

/* NEW */
fp = m4v_platform_popen(cmd, "r");
...
return m4v_platform_pclose_exit_code(fp);
```

#### Task 3.5 — Replace `2>/dev/null` literals (1 hour)

Every occurrence of the literal `2>/dev/null` in the command-building
`snprintf` calls must be replaced with `m4v_platform_null_redirect()`:

| Location | Line | Change |
|----------|------|--------|
| `probe_fps_for_input()` — avg_frame_rate probe | ~193 | `m4v_platform_null_redirect()` |
| `probe_fps_for_input()` — r_frame_rate probe | ~203 | `m4v_platform_null_redirect()` |
| `m4v_validate_input_supported()` — video codec | ~405 | `m4v_platform_null_redirect()` |
| `m4v_validate_input_supported()` — audio codec | ~422 | `m4v_platform_null_redirect()` |
| `m4v_create_from_input()` — chapters probe | ~606 | `m4v_platform_null_redirect()` |

Example:

```c
/* OLD */
snprintf(cmd, sizeof(cmd),
         "%s -v error -select_streams v:0 "
         "-show_entries stream=avg_frame_rate "
         "-of default=noprint_wrappers=1:nokey=1 %s 2>/dev/null",
         quoted_tool, quoted_input);

/* NEW */
snprintf(cmd, sizeof(cmd),
         "%s -v error -select_streams v:0 "
         "-show_entries stream=avg_frame_rate "
         "-of default=noprint_wrappers=1:nokey=1 %s %s",
         quoted_tool, quoted_input, m4v_platform_null_redirect());
```

#### Task 3.6 — Replace `make_temp_dir()` and `remove_temp_dir()` (1 hour)

```c
/* OLD — lines 309–337 */
static int make_temp_dir(char *path, size_t path_sz)
{
    char templ[1024];
    char *made;
    snprintf(templ, sizeof(templ), "/tmp/m4v_mux_XXXXXX");
    made = mkdtemp(templ);
    ...
}

static void remove_temp_dir(const char *dir)
{
    char cmd[2048];
    char quoted[1536];
    shell_quote(dir, quoted, sizeof(quoted));
    snprintf(cmd, sizeof(cmd), "rm -rf %s", quoted);
    system(cmd);
}

/* NEW — thin wrappers delegating to platform layer */
static int make_temp_dir(char *path, size_t path_sz)
{
    return m4v_platform_make_temp_dir(path, path_sz);
}

static void remove_temp_dir(const char *dir)
{
    m4v_platform_remove_temp_dir(dir);
}
```

#### Task 3.7 — Replace `linux_get_preferred_*()` calls (1 hour)

Three functions in `m4v.c` call Linux-specific binary resolvers directly. All
three must be replaced with the platform-abstracted equivalents:

| Old call | Line | Replacement |
|----------|------|-------------|
| `linux_get_preferred_ffprobe_bin()` | 400 | `m4v_platform_get_ffprobe_bin()` |
| `linux_get_preferred_ffmpeg_bin()` | 492 | `m4v_platform_get_ffmpeg_bin()` |
| `linux_get_preferred_ffprobe_bin()` | 493 | `m4v_platform_get_ffprobe_bin()` |
| `linux_get_preferred_mp4box_bin()` | 494 | `m4v_platform_get_mp4box_bin()` |

```c
/* OLD — lines 492–494 */
ffmpeg_bin  = linux_get_preferred_ffmpeg_bin();
ffprobe_bin = linux_get_preferred_ffprobe_bin();
mp4box_bin  = linux_get_preferred_mp4box_bin();

/* NEW */
ffmpeg_bin  = m4v_platform_get_ffmpeg_bin();
ffprobe_bin = m4v_platform_get_ffprobe_bin();
mp4box_bin  = m4v_platform_get_mp4box_bin();
```

#### Task 3.8 — Replace `access()` and `unlink()` in `m4v_create_from_input()` (0.5 hours)

```c
/* OLD — lines 587, 593, 638 */
if (!overwrite && access(output_file, F_OK) == 0) { ... }
if (overwrite)
    unlink(output_file);
...
unlink(output_file);

/* NEW */
if (!overwrite && m4v_platform_is_regular_file(output_file)) { ... }
if (overwrite)
    m4v_platform_unlink(output_file);
...
m4v_platform_unlink(output_file);
```

> **Semantic note:** The original `access(output_file, F_OK)` check detects
> the existence of *any* file-system entry (regular file, directory, symlink,
> etc.), whereas `m4v_platform_is_regular_file()` returns true only for
> regular files.  For the overwrite guard this is an intentional narrowing:
> if the output path exists but is a directory or special file, the subsequent
> MP4Box write will fail with a clear error rather than silently overwriting a
> non-regular entry.  If the original broad-existence semantics are required,
> add a separate `m4v_platform_file_exists()` wrapper that uses
> `GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES` on Windows and
> `access(path, F_OK) == 0` on POSIX, and use it for the overwrite check only.

#### Task 3.9 — Fix path separator in `m4v_make_output_name()` (1 hour)

```c
/* OLD — lines 366, 374 */
slash = strrchr(input_file, '/');
...
snprintf(out_file, out_file_sz, "%s/%s.m4v", output_dir, base);

/* NEW — also search for '\\' on Windows */
slash = strrchr(input_file, '/');
#if defined(_WIN32)
{
    const char *bslash = strrchr(input_file, '\\');
    if (!slash || (bslash && bslash > slash))
        slash = bslash;
}
#endif
...
/* '/' is accepted by Win32 APIs; keep for uniformity */
snprintf(out_file, out_file_sz, "%s/%s.m4v", output_dir, base);
```

#### Task 3.10 — Compile and test (3–5 hours)

| Step | Action |
|------|--------|
| 1 | Verify Linux build still compiles and tests pass |
| 2 | Cross-compile for Windows (MinGW or MSVC) |
| 3 | Run integration test: create an M4V from a short H.264+audio source |
| 4 | Verify all five pipeline stages complete successfully |
| 5 | Test environment variable overrides (`FFMPEG`, `FFPROBE_BIN`, `MP4BOX_BIN`) |
| 6 | Test missing tool error path produces `ERR_INVALID_OPTIONS` |

**Effort: 8–14 hours**

| Sub-task | Hours |
|----------|-------|
| Include replacement | 0.5 h |
| is_regular_file / unlink replacement | 0.5 h |
| shell_quote replacement | 0.5 h |
| popen/pclose replacement | 1 h |
| null redirect replacement | 1 h |
| make_temp_dir / remove_temp_dir | 1 h |
| linux_get_preferred_* replacement | 1 h |
| Path separator fix | 1 h |
| Build + integration testing | 3–5 h |

---

## 5. CMakeLists.txt Changes

### 5.1 `src/m4v/CMakeLists.txt` — updated

```cmake
# Cross-platform m4v module
set(M4V_SOURCES m4v.c)

if(WIN32)
    list(APPEND M4V_SOURCES
        platform/m4v_platform_windows.c
        ${CMAKE_SOURCE_DIR}/src/platform/windows/m4v_windows_probe.c
    )
else()
    list(APPEND M4V_SOURCES platform/m4v_platform_posix.c)
endif()

add_library(m4v STATIC ${M4V_SOURCES})

target_include_directories(m4v PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}           # for m4v_platform.h
    ${CMAKE_CURRENT_SOURCE_DIR}/platform  # optional convenience
)

target_link_libraries(m4v PRIVATE jansson)

if(UNIX AND NOT APPLE)
    target_link_libraries(m4v PRIVATE linux_runtime_probe)
endif()
```

### 5.2 Short-term option: disable on non-Linux (Variant A)

If full Windows support is not yet needed, the module can be excluded from
non-Linux builds with minimal CMake changes (2–4 hours effort):

```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(m4v STATIC m4v.c)
    target_link_libraries(m4v PRIVATE jansson linux_runtime_probe)
    target_include_directories(m4v PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
else()
    # m4v is Linux-only until Windows adaptation is complete
    add_library(m4v INTERFACE)
endif()
```

Callers in `main.c` / `cli_main.c` must be wrapped in `#if defined(__linux__)`
guards so the Windows linker does not attempt to reference an empty library.

---

## 6. Risk Assessment

| Risk | Severity | Likelihood | Mitigation |
|------|----------|------------|------------|
| `_popen` on Windows spawns `cmd.exe`, breaking POSIX shell quoting | HIGH | HIGH | Use `m4v_platform_shell_quote()` which emits double-quote style on Windows |
| `2>nul` must appear **after** all positional arguments in cmd.exe commands | MEDIUM | MEDIUM | Always append `m4v_platform_null_redirect()` at the end of the format string |
| `GetTempFileNameA` race condition: another process may claim the name between `DeleteFileA` and `CreateDirectoryA` | LOW | LOW | Acceptable for typical use; replace with UUID-based name if truly needed |
| `_pclose()` returns `-1` on Windows if command was not found | MEDIUM | MEDIUM | Add `== -1` check and propagate `ERR_FFMPEG_FAILED` (same as POSIX path) |
| MP4Box not commonly packaged on Windows | MEDIUM | HIGH | `m4v_windows_get_mp4box_bin()` searches GPAC installer paths and `MP4BOX_BIN` env var |
| PATH_MAX undefined on MSVC | LOW | HIGH | Add `#ifndef PATH_MAX / #define PATH_MAX 4096` in `m4v_platform.h` (done in Task 1.1) |
| `strtok_r` not available on MSVC | LOW | HIGH | Alias `strtok_s` as `strtok_r` in `m4v_platform.h` (done in Task 1.1) |
| Windows line endings (`\r\n`) in popen output | LOW | HIGH | Existing `strcspn(line, "\r\n")` already strips both — no additional change needed |
| `libfdk_aac` may not be included in pre-built Windows ffmpeg | MEDIUM | HIGH | Document prerequisite; fall back to `aac` encoder with a warning if `libfdk_aac` not found |
| Path construction inside MP4Box `-add` arguments requires correct quoting | MEDIUM | MEDIUM | `shell_quote_double()` (unchanged) handles `#`-delimited MP4Box track specs correctly |
