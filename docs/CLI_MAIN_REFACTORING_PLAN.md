# CLI_MAIN_REFACTORING_PLAN.md

Detailed refactoring and cross-platform adaptation plan for `src/cli/linux/main.c`
covering Windows and macOS targets.

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Current Architecture Analysis](#2-current-architecture-analysis)
3. [Detailed Function-by-Function Analysis](#3-detailed-function-by-function-analysis)
4. [Headers Analysis](#4-headers-analysis)
5. [Proposed New File Structure](#5-proposed-new-file-structure)
6. [Platform-Specific Differences](#6-platform-specific-differences)
7. [CMakeLists.txt Changes](#7-cmakeliststxt-changes)
8. [Implementation Phases](#8-implementation-phases)

---

## 1. Executive Summary

### 1.1 Current State

`src/cli/linux/main.c` is a **1,279-line** monolithic C file that implements
the entire interactive CLI entry point for the ffmpeg converter on Linux. A
parallel macOS version exists at `src/cli/macos/main.c`; no Windows
equivalent exists at all.

The Linux CLI entry point cannot be compiled or used on Windows or macOS
without modifications because it depends on:

- Linux VAAPI hardware acceleration (`h264_vaapi`, `hevc_vaapi`)
- `linux/runtime_probe.h` and `LinuxCodecSupport` struct
- POSIX-only APIs: `stat()`, `mkdir()`, `access()`, `getenv("HOME")`
- The `<termios.h>` and `<unistd.h>` POSIX headers
- Linux-specific default hardware device paths (`/dev/dri/renderD128`)

### 1.2 Task

Produce a complete architectural plan for splitting the single Linux file into
three platform-specific entry points that share as much logic as possible:

| Target file | Platform |
|-------------|----------|
| `src/cli/linux/main.c` | Linux (existing, reference) |
| `src/cli/macos/main.c` | macOS (partially exists, already diverged) |
| `src/cli/windows/main.c` | Windows (new — does not exist) |

Common logic shared across all three platforms must be extracted into a
shared header/source pair to avoid code duplication.

### 1.3 Code Structure (Linux reference)

- **Total lines:** 1,279
- **Architecture:** two execution modes
  - **Interactive menu mode** — entered when `argc == 1` (lines 744–1096)
  - **CLI argument mode** — entered when `argc > 1` (lines 220–376)
- **Two entry points:** `run_menu()` and `parse_args()`, both called from
  `main()` (lines 1163–1278)

### 1.4 Portability Summary

| Category | Approximate share | Notes |
|----------|------------------|-------|
| Pure logic (portable as-is) | ~40% | Callbacks, summary, menu state machine, parsing logic |
| POSIX API (wrappable) | ~30% | `stat`, `access`, `mkdir`, `fgets`, `getenv` |
| Linux-specific (must rewrite) | ~20% | Codec checks, VAAPI, `LinuxCodecSupport`, `/dev/dri` |
| Pure utility (portable) | ~10% | String parsing, validation, numeric conversion |

---

## 2. Current Architecture Analysis

### 2.1 Current File Structure

```
src/cli/
├── linux/
│   └── main.c          (1,279 lines — reference implementation)
├── macos/
│   └── main.c          (958 lines — partial macOS port, no mux support)
└── (windows/ — MISSING)
```

**Linux main.c high-level layout:**

| Lines | Region | Description |
|-------|--------|-------------|
| 1–17 | Includes | POSIX + project headers |
| 18 | Defines | `BUFFER_SIZE 4096` |
| 20–99 | Helper functions | Codec checks, file checks, path resolution |
| 101–127 | Mux helper | `run_cli_mux_postprocess()` |
| 133–172 | CLI callbacks | 8 callback functions for converter events |
| 178–214 | Usage/help | `print_usage()` — Linux VAAPI-aware |
| 220–376 | Argument parsing | `parse_args()` — mixed portability |
| 382–453 | Summary display | `print_summary()` |
| 462–741 | Menu utilities | `clear_screen`, `read_choice`, `read_output_dir`, etc. |
| 744–1096 | Main menu loop | `run_menu()` — 12-step state machine |
| 1098–1139 | File verification | `verify_all_files()` |
| 1141–1157 | Mux validation | `validate_mux_inputs()` |
| 1163–1278 | Entry point | `main()` |

### 2.2 Code Distribution by Portability

Based on line-by-line analysis of `src/cli/linux/main.c` (1,279 lines):

| Portability Category | Approx. Lines | Approx. % | Action |
|----------------------|---------------|-----------|--------|
| Pure logic — callbacks, summary, state machine | ~512 | ~40% | ✅ COPY AS-IS to all platforms |
| POSIX API — `stat`, `access`, `mkdir`, `fgets` | ~384 | ~30% | 🟡 WRAP per platform |
| Linux-specific — VAAPI, codec probing, `/dev/dri` | ~256 | ~20% | 🔴 REWRITE per platform |
| Pure utility — string parsing, validation | ~128 | ~10% | ✅ COPY AS-IS to all platforms |

### 2.3 Key Differences Between Existing Linux and macOS Versions

| Aspect | Linux (`src/cli/linux/main.c`) | macOS (`src/cli/macos/main.c`) |
|--------|-------------------------------|-------------------------------|
| Codec support | copy, prores, prores_ks, mux, h264_vaapi, hevc_vaapi | copy, prores, prores_ks, prores_videotoolbox, hevc_videotoolbox |
| Mux mode | ✅ Supported (step 10 in menu, `--video-track` in CLI) | ❌ Not present |
| Hardware probe | `linux_probe_codec_support()` at startup | None — codecs hardcoded |
| `run_menu()` signature | Takes `const LinuxCodecSupport* support` | No support struct |
| Menu step count | 12 steps (includes mux track step) | 10 steps |
| Buffer size | `BUFFER_SIZE 4096` for dir buffers | `PATH_MAX` for dir buffers |
| `print_usage()` signature | Takes `const LinuxCodecSupport* support` | No argument |
| `parse_args()` signature | Takes `const LinuxCodecSupport* support` | No support struct |
| VAAPI codec availability | Runtime probed | N/A |

---

## 3. Detailed Function-by-Function Analysis

### 3.1 Helper Functions (lines 20–99)

| Function | Lines | Type | Portability | Action |
|----------|-------|------|-------------|--------|
| `codec_is_mux()` | 20–22 | Pure logic | ✅ COPY AS-IS | Move to shared header |
| `codec_uses_software_prores()` | 24–27 | Pure logic | ✅ COPY AS-IS | Move to shared header |
| `codec_uses_linux_vaapi()` | 29–32 | Platform-specific | 🔴 REWRITE | Linux-only; macOS uses `codec_uses_videotoolbox()`, Windows uses `codec_uses_nvenc_qsv()` |
| `linux_codec_available()` | 34–50 | Platform-specific | 🔴 REWRITE | Depends on `LinuxCodecSupport`; replace with per-platform `codec_available()` |
| `apply_linux_default_hw_device()` | 52–61 | Platform-specific | 🔴 REWRITE | Sets VAAPI device path; macOS/Windows need different hw device logic |
| `linux_audio_output_mode_available()` | 63–70 | Pure logic | ✅ COPY AS-IS | Rename to `audio_output_mode_available()` in shared header |
| `file_is_regular_readable()` | 72–80 | POSIX API | 🟡 WRAP | Uses `stat()` + `S_ISREG` + `access(R_OK)`; wrap with `platform_file_is_regular_readable()` |
| `resolve_effective_output_dir()` | 82–99 | POSIX (HOME) | 🟡 WRAP | Uses `getenv("HOME")`; Windows needs `USERPROFILE`, provide `platform_get_home_dir()` |

**Notes on rewriting platform-specific helper functions:**

- **Windows `codec_available()`:** must accept NVENC (`h264_nvenc`, `hevc_nvenc`)
  and QSV (`h264_qsv`, `hevc_qsv`) codecs probed via `ffmpeg -encoders`.
- **macOS `codec_available()`:** VideoToolbox codecs (`prores_videotoolbox`,
  `hevc_videotoolbox`) are always available; no runtime probe needed.
- **Windows `apply_default_hw_device()`:** set `cuda` or `qsv` hw device
  depending on available encoder.

### 3.2 CLI Callbacks (lines 133–172) — ALL PORTABLE

All eight callback functions are completely platform-agnostic and must be
copied verbatim to every platform's `main.c` (or extracted into a shared
`cli_callbacks.h`):

| Function | Lines | Action |
|----------|-------|--------|
| `cli_on_file_begin()` | 133–136 | ✅ COPY AS-IS |
| `cli_on_file_end()` | 138–144 | ✅ COPY AS-IS |
| `cli_on_stage()` | 146–149 | ✅ COPY AS-IS |
| `cli_on_progress_encode()` | 151–153 | ✅ COPY AS-IS |
| `cli_on_progress_analysis()` | 155–157 | ✅ COPY AS-IS |
| `cli_on_message()` | 159–162 | ✅ COPY AS-IS |
| `cli_on_error()` | 164–167 | ✅ COPY AS-IS |
| `cli_on_complete()` | 169–172 | ✅ COPY AS-IS |

**Recommendation:** Extract all eight callbacks into
`src/cli/common/cli_callbacks.h` as `static inline` functions so every
platform's `main.c` can include a single header rather than duplicating
these functions.

### 3.3 Usage/Help (lines 178–214)

| Function | Lines | Type | Portability | Action |
|----------|-------|------|-------------|--------|
| `print_usage()` | 178–214 | Platform-specific | 🔴 REWRITE | Each platform lists its own codecs |

**Linux version** conditionally shows `h264_vaapi` / `hevc_vaapi` depending
on `LinuxCodecSupport.has_h264_vaapi` / `has_hevc_vaapi`.

**macOS version** (existing) lists fixed VideoToolbox codecs; does not accept
a `support` argument.

**Windows version** (new) must:
- List `h264_nvenc` / `hevc_nvenc` if NVENC is available
- List `h264_qsv` / `hevc_qsv` if Intel QSV is available
- Always list `copy`, `prores`, `prores_ks`, `mux`
- Include `--video-track` documentation (mux mode)
- Accept a `WindowsCodecSupport*` argument analogous to `LinuxCodecSupport*`

### 3.4 Argument Parsing (lines 220–376)

| Function | Lines | Type | Portability | Action |
|----------|-------|------|-------------|--------|
| `parse_args()` | 220–376 | Mixed | 🟡 REFACTOR | ~40% pure, ~60% platform-specific codec checks |

The argument parsing loop structure (option name matching, value extraction,
default initialization) is **fully portable**. The platform-specific parts are:

- Codec validation (`h264_vaapi`, `hevc_vaapi` guarded by `LinuxCodecSupport`)
- Audio output mode validation calls `linux_audio_output_mode_available()`

**Refactoring approach:**
1. Keep the loop structure as-is — it is pure C string logic.
2. Replace `linux_audio_output_mode_available()` with shared
   `audio_output_mode_available()`.
3. Replace VAAPI codec checks with a per-platform `codec_available()` call.
4. On Windows, add `--video-track` support (already present on Linux, absent
   on macOS).
5. Replace POSIX `stat()` + `access()` in `-o`/`--output` handling with
   `platform_dir_is_writable()`.

### 3.5 Summary & Display (lines 382–453)

| Function | Lines | Type | Portability | Action |
|----------|-------|------|-------------|--------|
| `print_summary()` | 382–453 | Pure logic | ✅ COPY AS-IS | Move to shared header |

`print_summary()` contains no platform calls. It only reads `ConvertOptions`
fields and calls `printf()` / `codec_is_mux()` / `codec_uses_software_prores()`.
The only change needed: the Linux version includes mux-specific output
(`video_track_path`, profile/deblock suppression for mux codec) which should
be preserved in all platform versions that support mux mode.

### 3.6 Menu Utilities (lines 462–741)

| Function | Lines | Type | Portability | Action |
|----------|-------|------|-------------|--------|
| `clear_screen()` | 462–465 | ANSI escape codes | ✅ COPY | ANSI works on Linux, macOS, and Windows 10+ |
| `read_choice()` | 469–482 | POSIX `fgets` | ✅ COPY | `fgets` is standard C; fully portable |
| `read_output_dir()` | 488–537 | Linux (HOME, `mkdir`) | 🔴 REWRITE | Uses `getenv("HOME")` and two-arg POSIX `mkdir()` |
| `process_input_path()` | 540–634 | Pure logic | ✅ COPY AS-IS | String processing only; handles quotes/escapes |
| `read_input_list()` | 641–715 | POSIX (`stat`, `S_ISREG`) | 🟡 WRAP | Replace `stat()`/`S_ISREG` with `platform_file_is_regular_readable()` |
| `read_single_file_path()` | 717–741 | POSIX (`stat`, `access`) | 🟡 WRAP | Replace `stat()`/`access()` with `platform_file_is_regular_readable()` |

**`read_output_dir()` rewrite requirements by platform:**

| Operation | Linux | macOS | Windows |
|-----------|-------|-------|---------|
| Default path | `$HOME/ffmpeg_converter` | `$HOME/ffmpeg_converter` | `%USERPROFILE%\ffmpeg_converter` |
| Home env var | `getenv("HOME")` | `getenv("HOME")` | `getenv("USERPROFILE")` or `SHGetKnownFolderPath()` |
| Dir creation | `mkdir(path, 0755)` | `mkdir(path, 0755)` | `_mkdir(path)` or `CreateDirectoryA()` |
| Write check | `access(path, W_OK)` | `access(path, W_OK)` | `_access(path, 2)` or `GetFileAttributes()` |
| Path separator | `/` | `/` | `\` (accept both) |

### 3.7 Main Menu Loop (lines 744–1096)

| Function | Lines | Type | Portability | Action |
|----------|-------|------|-------------|--------|
| `run_menu()` | 744–1096 | Mixed | 🟡 REFACTOR | ~70% pure state machine, ~30% platform calls |

The menu is a 12-step state machine (steps 1–12). The state transitions,
user input handling, and option assignment logic are **fully portable**:

**Steps portable as-is:**
- Step 1: Codec selection (must list platform codecs)
- Step 2: Profile selection (`lt`/`standard`/`hq`/`4444`)
- Step 3: Deblock selection
- Step 4: Audio normalization selection
- Step 5: Genre selection (for loudnorm2)
- Step 6: Audio output mode selection
- Step 7: Overwrite selection
- Step 8: Output directory → calls `read_output_dir()` (wrap)
- Step 9: Input file list → calls `read_input_list()` (wrap)
- Step 10: Mux video-track → calls `read_single_file_path()` (wrap, Linux-only currently)
- Step 11: Finalize options and call `apply_linux_default_hw_device()` (rewrite)

**Platform-specific parts of `run_menu()`:**

1. **Codec list construction** (lines 767–782): Linux dynamically builds
   `codec_names[]` / `codec_steps[]` from `LinuxCodecSupport`. Each platform
   must build its own codec list. Windows must probe NVENC/QSV at startup.

2. **`apply_linux_default_hw_device()` call** (line 1074): Linux sets
   `opts->hw_device` from `support->default_render_node`. Each platform
   must call its own `apply_default_hw_device()`.

3. **Function signature**: Linux takes `const LinuxCodecSupport* support`.
   macOS does not. Windows should take `const WindowsCodecSupport* support`.
   Consider abstracting to a `PlatformCodecSupport` typedef.

### 3.8 File Verification (lines 1098–1139)

| Function | Lines | Type | Portability | Action |
|----------|-------|------|-------------|--------|
| `verify_all_files()` | 1098–1139 | POSIX (`stat`, `access`) | 🟡 WRAP | Replace `stat()`/`S_ISREG`/`access()` with `platform_file_is_regular_readable()` |

The overall verification loop structure (iterate, print result, count valid,
ask user whether to continue) is **fully portable**. Only the three POSIX
checks need wrapping.

### 3.9 Validation (lines 1141–1157)

| Function | Lines | Type | Portability | Action |
|----------|-------|------|-------------|--------|
| `validate_mux_inputs()` | 1141–1157 | Pure logic + POSIX wrap | 🟡 WRAP | Calls `codec_is_mux()` (portable) and `file_is_regular_readable()` (wrap) |

`validate_mux_inputs()` itself is portable once `file_is_regular_readable()`
is wrapped. It should be included in all platforms that support mux mode
(Linux, Windows). macOS currently does not support mux mode.

### 3.10 Main Entry (lines 1163–1278)

| Function | Lines | Type | Portability | Action |
|----------|-------|------|-------------|--------|
| `main()` | 1163–1278 | Mixed | 🔴 REWRITE (per platform) | Calls `linux_probe_codec_support()`, `run_menu()`, `parse_args()` |

**Platform-specific parts of `main()`:**

1. **Codec support probing** (lines 1171–1172): `linux_probe_codec_support()`
   is Linux-only. macOS does not probe. Windows must probe NVENC/QSV support
   via `ffmpeg -encoders`.

2. **`run_menu()` call** (line 1203): Linux passes `&support`. Platform-
   specific structs must match per-platform `run_menu()` signature.

3. **`parse_args()` call** (line 1215): Same issue — Linux passes `&support`.

4. **`apply_linux_default_hw_device()` call** (line 1221): Called after CLI
   mode parsing. Needs per-platform replacement.

5. **`run_cli_mux_postprocess()` call** (line 1260): Mux post-processing.
   Portable (calls `mux_run_postprocess()`), but only needed on platforms
   with mux support (Linux, Windows).

**Portable parts of `main()`:**

- `converter_create()` / `converter_destroy()` calls
- `ConverterCallbacks` struct initialization
- `converter_set_callbacks()` / `converter_set_options()` calls
- `converter_process_files()` call
- Memory cleanup logic (`goto cleanup`, `free()` loop)
- Result code mapping (`ERR_OK` → 0, else 1)

---

## 4. Headers Analysis

### 4.1 Standard C Headers (portable across all platforms)

| Header | Line | Type | Platform | Action |
|--------|------|------|----------|--------|
| `<stdio.h>` | 1 | Standard C | All | ✅ Keep as-is |
| `<string.h>` | 2 | Standard C | All | ✅ Keep as-is |
| `<stdlib.h>` | 3 | Standard C | All | ✅ Keep as-is |
| `<ctype.h>` | 4 | Standard C | All | ✅ Keep as-is |
| `<stdbool.h>` | 5 | Standard C99 | All (MSVC 2015+) | ✅ Keep as-is |
| `<errno.h>` | 9 | Standard C | All | ✅ Keep as-is |
| `<stddef.h>` | 10 | Standard C | All | ✅ Keep as-is |

### 4.2 POSIX Headers (require platform-specific handling)

| Header | Line | Type | Platform | Windows Replacement |
|--------|------|------|----------|---------------------|
| `<sys/stat.h>` | 6 | POSIX | Linux / macOS | `<sys/stat.h>` exists in MSVC/MinGW but `S_ISREG`, `S_ISDIR` macros are missing; add `#ifndef _WIN32` guards or define compat macros |
| `<sys/types.h>` | 7 | POSIX | Linux / macOS | Remove — transitively included by `<sys/stat.h>` on all platforms that need it |
| `<unistd.h>` | 8 | POSIX | Linux / macOS | Remove on Windows; replace `access()` with `_access()` from `<io.h>`, replace `getcwd()` etc. with Win32 equivalents |
| `<termios.h>` | 11 | POSIX terminal | Linux / macOS | Remove on Windows entirely — not used in practice (raw terminal mode is not used in the current implementation; `fgets` is used instead) |

### 4.3 Project Headers

| Header | Line | Type | Platform | Windows / macOS Replacement |
|--------|------|------|----------|-----------------------------|
| `"converter.h"` | 13 | Project | All | ✅ Keep as-is — public API |
| `"mux.h"` | 14 | Project | Linux (+ Windows) | ✅ Keep on Linux/Windows; omit on macOS (no mux support) |
| `"linux/runtime_probe.h"` | 15 | Linux-specific | Linux only | Replace with `"windows/runtime_probe.h"` on Windows; omit on macOS |
| `"progress.h"` | 16 | Project | All | ✅ Keep as-is |

### 4.4 Recommended Windows-Specific Additions

```c
#ifdef _WIN32
#  include <windows.h>      /* CreateDirectory, GetFileAttributes */
#  include <direct.h>       /* _mkdir */
#  include <io.h>           /* _access */
#  define S_ISREG(m)  (((m) & _S_IFMT) == _S_IFREG)
#  define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#  define access      _access
#  define W_OK        2
#  define R_OK        4
#endif
```

### 4.5 Recommended macOS-Specific Additions

```c
#ifdef __APPLE__
#  include <limits.h>       /* PATH_MAX */
#  include <sys/stat.h>     /* stat, mkdir */
#  include <unistd.h>       /* access */
#endif
```

---

## 5. Proposed New File Structure

```
src/cli/
├── common/
│   ├── cli_callbacks.h      (NEW — 8 portable callback functions as static inline)
│   └── cli_common.h         (NEW — codec_is_mux, codec_uses_software_prores,
│                                    audio_output_mode_available, print_summary)
├── linux/
│   └── main.c               (REFACTORED — uses common headers, keeps VAAPI logic)
├── macos/
│   └── main.c               (REFACTORED — uses common headers, keeps VT logic)
└── windows/
    └── main.c               (NEW — NVENC/QSV, Win32 path handling)
```

### 5.1 File Roles

| File | Role | Status |
|------|------|--------|
| `src/cli/common/cli_callbacks.h` | All 8 callback functions as `static inline` | NEW |
| `src/cli/common/cli_common.h` | Shared pure-logic functions and `print_summary` | NEW |
| `src/cli/linux/main.c` | Linux entry point — VAAPI, `LinuxCodecSupport`, mux | REFACTORED |
| `src/cli/macos/main.c` | macOS entry point — VideoToolbox, no mux | REFACTORED |
| `src/cli/windows/main.c` | Windows entry point — NVENC/QSV, `WindowsCodecSupport`, mux | NEW |

### 5.2 Contents of `src/cli/common/cli_callbacks.h`

```c
/* cli_callbacks.h — static inline CLI callbacks shared by all platforms */
#pragma once
#include "converter.h"
#include "progress.h"

static inline void cli_on_file_begin(const char* filename, int index, int total) { ... }
static inline void cli_on_file_end(const char* filename, ConverterError status)  { ... }
static inline void cli_on_stage(const char* stage)                               { ... }
static inline void cli_on_progress_encode(float percent, float fps, float eta)   { ... }
static inline void cli_on_progress_analysis(float percent, float eta)            { ... }
static inline void cli_on_message(const char* text)                              { ... }
static inline void cli_on_error(const char* text, ConverterError code)           { ... }
static inline void cli_on_complete(void)                                         { ... }
```

### 5.3 Contents of `src/cli/common/cli_common.h`

```c
/* cli_common.h — shared pure-logic functions */
#pragma once
#include "converter.h"
#include <stdio.h>
#include <string.h>

static inline int codec_is_mux(const char* codec)                 { ... }
static inline int codec_uses_software_prores(const char* codec)   { ... }
static inline int audio_output_mode_available(const char* mode)   { ... }
static inline void print_summary(const ConvertOptions* opts,
                                 const char** files,
                                 int file_count)                   { ... }
static inline int process_input_path(const char* input,
                                     char* output,
                                     size_t out_size)              { ... }
static inline void clear_screen(void)                              { ... }
static inline int  read_choice(void)                               { ... }
```

---

## 6. Platform-Specific Differences

### 6.1 Codec Sets

| Codec | Linux | macOS | Windows |
|-------|-------|-------|---------|
| `copy` | ✅ | ✅ | ✅ |
| `prores` | ✅ | ✅ | ✅ |
| `prores_ks` | ✅ | ✅ | ✅ |
| `mux` | ✅ | ❌ | ✅ |
| `h264_vaapi` | ✅ (runtime probe) | ❌ | ❌ |
| `hevc_vaapi` | ✅ (runtime probe) | ❌ | ❌ |
| `prores_videotoolbox` | ❌ | ✅ (always available) | ❌ |
| `hevc_videotoolbox` | ❌ | ✅ (always available) | ❌ |
| `h264_nvenc` | ❌ | ❌ | ✅ (runtime probe) |
| `hevc_nvenc` | ❌ | ❌ | ✅ (runtime probe) |
| `h264_qsv` | ❌ | ❌ | ✅ (runtime probe) |
| `hevc_qsv` | ❌ | ❌ | ✅ (runtime probe) |

### 6.2 Runtime Probe Structs

**Linux** (`linux/runtime_probe.h`):
```c
typedef struct {
    int has_h264_vaapi;
    int has_hevc_vaapi;
    char default_render_node[1024];  /* /dev/dri/renderD128 */
    char ffmpeg_bin[1024];
    /* ... */
} LinuxCodecSupport;
```

**macOS** — no probe struct; VideoToolbox codecs always available.

**Windows** (new — `windows/runtime_probe.h`):
```c
typedef struct {
    int has_h264_nvenc;
    int has_hevc_nvenc;
    int has_h264_qsv;
    int has_hevc_qsv;
    char ffmpeg_bin[MAX_PATH];
    char ffprobe_bin[MAX_PATH];
    char mkvmerge_bin[MAX_PATH];
    char mp4box_bin[MAX_PATH];
} WindowsCodecSupport;

int windows_probe_codec_support(WindowsCodecSupport* out);
```

### 6.3 Hardware Device Assignment

| Platform | Function | Device string |
|----------|----------|---------------|
| Linux | `apply_linux_default_hw_device()` | `/dev/dri/renderD128` (from probe) |
| macOS | None needed | VideoToolbox selected by codec name |
| Windows (NVENC) | `apply_windows_default_hw_device()` | `cuda` |
| Windows (QSV) | `apply_windows_default_hw_device()` | `qsv` |

### 6.4 Home Directory / Default Output Path

| Platform | Environment variable | Fallback | Default output path |
|----------|---------------------|----------|---------------------|
| Linux | `$HOME` | `.` | `$HOME/ffmpeg_converter` |
| macOS | `$HOME` | `.` | `$HOME/ffmpeg_converter` |
| Windows | `%USERPROFILE%` | `%HOMEDRIVE%%HOMEPATH%` | `%USERPROFILE%\ffmpeg_converter` |

### 6.5 Directory Creation

| Platform | API | Signature |
|----------|-----|-----------|
| Linux | POSIX | `mkdir(path, 0755)` |
| macOS | POSIX | `mkdir(path, 0755)` |
| Windows (MinGW/MSVC) | CRT | `_mkdir(path)` |
| Windows (Win32) | Win32 | `CreateDirectoryA(path, NULL)` |

### 6.6 File Access Check

| Platform | API | Read check | Write check |
|----------|-----|-----------|-------------|
| Linux | POSIX | `access(path, R_OK)` | `access(path, W_OK)` |
| macOS | POSIX | `access(path, R_OK)` | `access(path, W_OK)` |
| Windows | CRT | `_access(path, 4)` | `_access(path, 2)` |

### 6.7 Menu Step Comparison

| Step | Linux | macOS | Windows (planned) |
|------|-------|-------|-------------------|
| 1 — Codec | Dynamic (from probe) | Fixed VT list | Dynamic (from probe) |
| 2 — Profile | ✅ | ✅ | ✅ |
| 3 — Deblock | ✅ | ✅ | ✅ |
| 4 — Audio norm | ✅ | ✅ | ✅ |
| 5 — Genre | ✅ | ✅ | ✅ |
| 6 — Audio output | ✅ | ❌ | ✅ |
| 7 — Overwrite | ✅ | ✅ | ✅ |
| 8 — Output dir | ✅ | ✅ | ✅ (Win32 paths) |
| 9 — Input files | ✅ | ✅ | ✅ |
| 10 — Mux track | ✅ | ❌ | ✅ |
| 11 — Finalize | ✅ | ✅ | ✅ |

---

## 7. CMakeLists.txt Changes

### 7.1 Current State (`src/cli/CMakeLists.txt`)

The current CMake configuration selects between Linux and macOS CLI source
based on the detected platform. Windows is not handled.

### 7.2 Required Changes

```cmake
if(WIN32)
    set(CLI_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/windows/main.c")
    set(CLI_PLATFORM_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/../../platform/windows/runtime_probe.h")
elseif(APPLE)
    set(CLI_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/macos/main.c")
else()
    # Linux
    set(CLI_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/linux/main.c")
    set(CLI_PLATFORM_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/../../platform/linux/runtime_probe.h")
endif()

target_sources(ffmpeg_converter_cli PRIVATE ${CLI_MAIN_SOURCE})
target_include_directories(ffmpeg_converter_cli PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../platform/${PLATFORM_DIR}"
)
```

### 7.3 New `windows/runtime_probe.c` Integration

A new `src/platform/windows/runtime_probe.c` must be created and linked
into the Windows CLI build, analogously to
`src/platform/linux/runtime_probe.c`:

```cmake
if(WIN32)
    target_sources(ffmpeg_converter_cli PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../platform/windows/runtime_probe.c"
    )
endif()
```

---

## 8. Implementation Phases

### Phase 1 — Extract Common Headers (no behavior change)

**Goal:** Reduce code duplication between Linux and macOS without any
functional change.

**Tasks:**

1. Create `src/cli/common/cli_callbacks.h` — extract all 8 callback functions
   as `static inline`.
2. Create `src/cli/common/cli_common.h` — extract:
   - `codec_is_mux()`
   - `codec_uses_software_prores()`
   - `audio_output_mode_available()` (renamed from `linux_audio_output_mode_available()`)
   - `print_summary()`
   - `process_input_path()`
   - `clear_screen()`
   - `read_choice()`
3. Update `src/cli/linux/main.c` to include the new common headers and remove
   the duplicated definitions.
4. Update `src/cli/macos/main.c` to include the new common headers.
5. Verify: both Linux and macOS builds compile and run identically to before.

**Estimated effort:** 2–4 hours.

### Phase 2 — Wrap POSIX File APIs

**Goal:** Replace direct POSIX calls with thin wrappers to make future
porting mechanical.

**Tasks:**

1. In `src/cli/linux/main.c`, replace all direct `stat()` / `S_ISREG` /
   `access(R_OK)` calls with a local `file_is_regular_readable()` wrapper
   (already exists — just ensure consistent use).
2. Replace `stat()` + `S_ISDIR` + `access(W_OK)` in `read_output_dir()` with
   a `dir_is_writable()` helper.
3. Replace `getenv("HOME")` with a `get_home_dir()` helper function.
4. Replace two-argument `mkdir()` with a `make_dir()` helper.
5. Document the wrapper API in `src/cli/common/cli_common.h`:

```c
/*
 * Platform shim — implemented differently per platform main.c.
 * Declaration here; definition in each platform's main.c.
 */
static int  file_is_regular_readable(const char* path);
static int  dir_is_writable(const char* path);
static void make_dir(const char* path);
static const char* get_home_dir(void);
```

6. Verify: Linux builds and runs identically.

**Estimated effort:** 2–3 hours.

### Phase 3 — macOS Alignment

**Goal:** Bring `src/cli/macos/main.c` up to parity with the common-header
approach from Phases 1–2, and optionally add mux support.

**Tasks:**

1. Include `cli_callbacks.h` and `cli_common.h` in `src/cli/macos/main.c`.
2. Remove duplicated definitions.
3. Implement macOS-specific `file_is_regular_readable()`, `dir_is_writable()`,
   `get_home_dir()` using POSIX (same as Linux).
4. (Optional) Add mux mode to macOS menu and `parse_args()`.
5. Verify: macOS builds and runs identically.

**Estimated effort:** 3–5 hours.

### Phase 4 — Windows `runtime_probe.h` / `runtime_probe.c`

**Goal:** Create the Windows codec probe infrastructure analogous to Linux.

**Tasks:**

1. Create `src/platform/windows/runtime_probe.h`:
   - Define `WindowsCodecSupport` struct.
   - Declare `windows_probe_codec_support()`.
2. Create `src/platform/windows/runtime_probe.c`:
   - Implement binary search: env vars → bundled → PATH.
   - Implement NVENC/QSV probe via `ffmpeg -encoders` output parsing.
   - Implement `windows_get_home_dir()` using `USERPROFILE`.
3. Update `CMakeLists.txt` to compile `runtime_probe.c` on Windows.
4. Verify: compiles on Windows (MSYS2 MinGW or MSVC).

**Estimated effort:** 6–10 hours.

### Phase 5 — Windows `main.c`

**Goal:** Create the complete Windows CLI entry point.

**Tasks:**

1. Create `src/cli/windows/main.c`.
2. Include `cli_callbacks.h` and `cli_common.h`.
3. Include `windows/runtime_probe.h`.
4. Implement Windows-specific functions:
   - `codec_available()` — checks `WindowsCodecSupport`
   - `apply_windows_default_hw_device()` — sets `cuda` or `qsv`
   - `print_usage()` — lists NVENC/QSV codecs conditionally
   - `parse_args()` — same structure as Linux, Windows codec checks
   - `read_output_dir()` — uses `%USERPROFILE%`, `_mkdir()`, `_access()`
   - `file_is_regular_readable()` — uses `_stat()`, `_S_IFREG`, `_access(4)`
   - `dir_is_writable()` — uses `_stat()`, `_S_IFDIR`, `_access(2)`
   - `run_menu()` — dynamic codec list from `WindowsCodecSupport`
   - `main()` — calls `windows_probe_codec_support()` at startup
5. Add Windows-specific compat defines at top of file:
   ```c
   #ifdef _WIN32
   #  include <windows.h>
   #  include <direct.h>
   #  include <io.h>
   #  define access  _access
   #  define R_OK    4
   #  define W_OK    2
   #  define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
   #  define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
   #endif
   ```
6. Verify: compiles and runs on Windows (MSYS2 + MinGW-w64).

**Estimated effort:** 8–14 hours.

### Phase 6 — Integration and Testing

**Tasks:**

1. Update top-level `CMakeLists.txt` to select the correct CLI source.
2. Run the full build matrix: Linux, macOS, Windows.
3. Manual end-to-end test on each platform:
   - Interactive menu: run without arguments, verify all steps work.
   - CLI mode: `ffmpeg_converter -c prores_ks input.mov`, verify summary and output.
   - Hardware codec: test VAAPI on Linux, VideoToolbox on macOS, NVENC on Windows.
   - Mux mode: test on Linux and Windows.
4. Fix any build or runtime issues.

**Estimated effort:** 4–8 hours.

---

## Summary Table

| Item | Linux | macOS | Windows |
|------|-------|-------|---------|
| File | `src/cli/linux/main.c` | `src/cli/macos/main.c` | `src/cli/windows/main.c` (NEW) |
| Lines | 1,279 | 958 | ~900 (estimated) |
| HW accel | VAAPI (runtime probe) | VideoToolbox (always) | NVENC / QSV (runtime probe) |
| Mux mode | ✅ | ❌ | ✅ |
| Probe struct | `LinuxCodecSupport` | None | `WindowsCodecSupport` (NEW) |
| Home dir | `$HOME` | `$HOME` | `%USERPROFILE%` |
| Dir create | `mkdir(p, 0755)` | `mkdir(p, 0755)` | `_mkdir(p)` |
| File check | `stat` + `access` | `stat` + `access` | `_stat` + `_access` |
| Menu steps | 12 | 10 | 12 |
| Common headers | `cli_callbacks.h`, `cli_common.h` | same | same |
