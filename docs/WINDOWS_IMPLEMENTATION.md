# WINDOWS_IMPLEMENTATION.md

Complete guide to the Windows-specific implementation of `converter.c` —
covering architecture, binary resolution, GPU acceleration, path handling,
and concrete command examples.

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Windows Architecture Overview](#2-windows-architecture-overview)
3. [Binary Resolution Strategy](#3-binary-resolution-strategy)
4. [Video Codecs on Windows](#4-video-codecs-on-windows)
5. [Audio Codecs on Windows](#5-audio-codecs-on-windows)
6. [GPU Acceleration on Windows](#6-gpu-acceleration-on-windows)
7. [Audio Filters — Mandatory Dependencies](#7-audio-filters--mandatory-dependencies)
8. [Path Handling Issues & Solutions](#8-path-handling-issues--solutions)
9. [Pipe & Output Handling Issues](#9-pipe--output-handling-issues)
10. [Home Directory Handling](#10-home-directory-handling)
11. [Directory Creation](#11-directory-creation)
12. [Tool Resolution Scenarios](#12-tool-resolution-scenarios)
13. [Windows-Specific Error Codes](#13-windows-specific-error-codes)
14. [Windows-Specific Implementation Details](#14-windows-specific-implementation-details)

---

## 1. Executive Summary

### 1.1 Windows-Specific Problems of the Converter

`converter.c` was originally written for Linux and macOS. The following
problems prevent it from compiling or running correctly on Windows:

| Severity | Problem | Details |
|----------|---------|---------|
| CRITICAL | `#include <unistd.h>` | POSIX-only header — not present on MSVC/MinGW |
| CRITICAL | `readlink("/proc/self/exe")` | Linux-only — no `/proc` on Windows |
| CRITICAL | POSIX `mkdir(path, mode)` | Windows uses `_mkdir(path)` — different signature |
| CRITICAL | Shell redirect `2>/dev/null` | Windows equivalent is `2>nul` |
| CRITICAL | `sysconf(_SC_NPROCESSORS_ONLN)` | POSIX-only — Windows uses `GetSystemInfo()` |
| CRITICAL | `access(path, X_OK)` | POSIX-only — Windows uses `_access()` or `.exe` check |
| HIGH | No `ffmpeg.exe` PATH search | Converter assumes fixed POSIX paths |
| HIGH | No `USERPROFILE` home dir handling | Linux `HOME` env var not set on Windows |
| HIGH | Mixed path separators | `\` vs `/` confusion in path operations |
| HIGH | CRLF line endings from popen | Windows tools output `\r\n`, not `\n` |
| HIGH | No GPU codec detection | NVENC/QSV detection missing |
| HIGH | No audio filter validation | Filters must be probed at startup |

### 1.2 Solutions Implemented

All problems are resolved through the platform abstraction layer:

```
converter.c  →  converter_platform.h  →  platform/converter_windows.c
```

- `platform/converter_windows.c` implements every `platform_*()` function
  using Win32 APIs (`windows.h`, `shlwapi.h`, `direct.h`, `io.h`)
- `converter.c` has all platform-specific code (binary resolution, path
  operations, GPU detection) moved to the platform layer; only minimal
  `#ifdef _WIN32` guards for include selection may remain at the top level
- `CMakeLists.txt` selects the correct platform source file at build time

### 1.3 Differences Between Windows and Linux/macOS

| Aspect | Linux/macOS | Windows |
|--------|-------------|---------|
| Exe location | `/proc/self/exe` / `_NSGetExecutablePath()` | `GetModuleFileNameW()` |
| Binary paths | Predictable (`/usr/bin`, `/opt/homebrew/bin`) | Chaotic (MSYS2, Chocolatey, custom) |
| Dir creation | `mkdir(path, 0755)` | `_mkdir(path)` |
| Home dir | `$HOME` | `%USERPROFILE%` (or `HOMEDRIVE+HOMEPATH`) |
| CPU count | `sysconf(_SC_NPROCESSORS_ONLN)` | `GetSystemInfo()` |
| GPU accel | VAAPI (Linux), VideoToolbox (macOS) | NVENC (NVIDIA), QSV (Intel) |
| Null device | `/dev/null` | `nul` |
| Line endings | `\n` | `\r\n` |
| Exec check | `access(path, X_OK)` | `.exe` extension check |
| Shell redirect | `2>/dev/null` | `2>nul` |

---

## 2. Windows Architecture Overview

### KEY DIFFERENCE: Binary Path Chaos

On Linux and macOS, tools are installed in well-known, predictable locations:

```
Linux/macOS: Unified, predictable tool paths
────────────────────────────────────────────────────────────────────
Linux:
  /usr/bin/ffmpeg
  /usr/local/bin/ffmpeg

macOS (Homebrew):
  /opt/homebrew/bin/ffmpeg      (Apple Silicon)
  /usr/local/bin/ffmpeg         (Intel)

macOS (MacPorts):
  /opt/local/bin/ffmpeg

→ A short list of directories is enough to find any tool.
```

On Windows, every package manager installs to a completely different location:

```
Windows: CHAOTIC tool installation paths
────────────────────────────────────────────────────────────────────
MSYS2/pacman:
  C:\msys64\mingw64\bin\ffmpeg.exe
  C:\msys64\mingw64\bin\ffprobe.exe
  C:\msys64\mingw64\bin\mkvmerge.exe

Chocolatey:
  C:\ProgramData\chocolatey\bin\ffmpeg.exe
  C:\ProgramData\chocolatey\lib\gpac\tools\mp4box.exe

Winget / standalone:
  C:\Program Files\ffmpeg\bin\ffmpeg.exe
  C:\Program Files (x86)\MKVToolNix\mkvmerge.exe

User-chosen / custom:
  D:\tools\ffmpeg\ffmpeg.exe
  E:\video\bin\ffmpeg.exe

→ NO PREDICTABLE PATH → ACTIVE SEARCH AND VALIDATION REQUIRED
```

### Problem Diagram

```
┌───────────────────────────────────────────────────────────────────┐
│                     LINUX / macOS                                 │
│                                                                   │
│  converter.c                                                      │
│      │                                                            │
│      └──► check /usr/bin/ffmpeg  ──► FOUND ──► use it            │
│           check /usr/local/bin/ffmpeg                             │
│           check /opt/homebrew/bin/ffmpeg                          │
│                                                                   │
│  Short, ordered list. Rarely fails.                               │
└───────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────┐
│                        WINDOWS                                    │
│                                                                   │
│  converter.c                                                      │
│      │                                                            │
│      ├──► ENV VAR (FFMPEG_BIN)?  ──► yes ──► use it              │
│      │                                                            │
│      ├──► Bundled (same dir as .exe)?  ──► yes ──► use it        │
│      │                                                            │
│      ├──► MSYS2 (C:\msys64\mingw64\bin\)?  ──► yes ──► use it    │
│      │                                                            │
│      ├──► Chocolatey (C:\ProgramData\...)?  ──► yes ──► use it   │
│      │                                                            │
│      ├──► System PATH search?  ──► yes ──► use it                │
│      │                                                            │
│      └──► NOT FOUND ──► ERROR                                     │
│                                                                   │
│  Six priority levels. Complex but necessary.                      │
└───────────────────────────────────────────────────────────────────┘
```

---

## 3. Binary Resolution Strategy

### 3.1 Priority Order for Binary Search

Each tool (`ffmpeg`, `ffprobe`, `mkvmerge`, `MP4Box`) is resolved using the
same six-level priority chain. Higher priority always wins:

```
Priority 1: Environment Variables
           FFMPEG_BIN, FFPROBE_BIN, MKVMERGE_BIN, MP4BOX_BIN
           → User has explicit control. Checked first, always.

Priority 2: Bundled Binary
           Same directory as the converter .exe
           → Enables portable/self-contained deployment.

Priority 3: MSYS2 Installation
           C:\msys64\mingw64\bin\<tool>.exe
           → Most common dev installation on Windows.

Priority 4: Chocolatey Installation
           C:\ProgramData\chocolatey\bin\<tool>.exe
           C:\ProgramData\chocolatey\lib\gpac\tools\mp4box.exe
           → Used for tools better supported by Chocolatey (e.g., GPAC).

Priority 5: System PATH Search
           PathFindOnPathA() or equivalent
           → Covers Winget, standalone installs, custom PATH entries.

Priority 6: NOT FOUND → ERROR
           Fails with ERR_BINARY_NOT_FOUND and a clear error message.
```

### 3.2 Flowchart (Binary Resolution for ffmpeg)

```
START: ffmpeg needed
         │
         ▼
 ┌─────────────────────┐
 │ Check FFMPEG_BIN    │
 │ environment var     │
 └────────┬────────────┘
          │ set and file exists?
     YES ◄─┤
          │ NO
          ▼
 ┌─────────────────────┐
 │ Check bundled:      │
 │ <exe_dir>\ffmpeg.exe│
 └────────┬────────────┘
          │ file exists?
     YES ◄─┤
          │ NO
          ▼
 ┌──────────────────────────────┐
 │ Check MSYS2:                 │
 │ C:\msys64\mingw64\bin\       │
 │ ffmpeg.exe                   │
 └────────┬─────────────────────┘
          │ file exists?
     YES ◄─┤
          │ NO
          ▼
 ┌──────────────────────────────┐
 │ Check Chocolatey:            │
 │ C:\ProgramData\chocolatey\   │
 │ bin\ffmpeg.exe               │
 └────────┬─────────────────────┘
          │ file exists?
     YES ◄─┤
          │ NO
          ▼
 ┌─────────────────────┐
 │ Search system PATH  │
 │ PathFindOnPathA()   │
 └────────┬────────────┘
          │ found?
     YES ◄─┤
          │ NO
          ▼
    ERROR: NOT FOUND
    ERR_BINARY_NOT_FOUND

     YES ──► Return resolved path
             Cache in platform state
             Continue initialization
```

### 3.3 Current Test Environment (Real Example)

This section documents the actual mixed-source installation used for
development and testing of this converter on Windows:

#### ffmpeg + ffprobe

```
Source:    MSYS2 pacman
Command:   pacman -S mingw-w64-x86_64-ffmpeg
Path:      C:\msys64\mingw64\bin\ffmpeg.exe
           C:\msys64\mingw64\bin\ffprobe.exe
Features:  libsoxr compiled in by default; libfdk-aac may require
           mingw-w64-x86_64-ffmpeg-full (non-free build) due to licensing
Notes:     MSYS2 ffmpeg has full codec support; Chocolatey builds may lack
           libfdk-aac and/or libsoxr — always verify with: ffmpeg -buildconf
```

#### mkvmerge

```
Source:    MSYS2 pacman
Command:   pacman -S mingw-w64-x86_64-mkvtoolnix
Path:      C:\msys64\mingw64\bin\mkvmerge.exe
Notes:     Part of the mkvtoolnix suite
```

#### MP4Box (GPAC)

```
Source:    Chocolatey
Command:   choco install gpac
Path:      C:\ProgramData\chocolatey\lib\gpac\tools\mp4box.exe
           (also may be on PATH as mp4box.exe)
Reason:    GPAC is simpler to install via Chocolatey than MSYS2
           MSYS2 does not package GPAC in mingw64 by default
```

#### Why Different Package Managers?

```
FFmpeg family (ffmpeg, ffprobe, mkvmerge):
  → MSYS2 provides the most complete build with all codecs and filters.
  → Chocolatey's ffmpeg build may omit libfdk-aac, libsoxr.

GPAC (MP4Box):
  → Not available in MSYS2 mingw64.
  → Chocolatey provides a straightforward install.

Rule: Always prefer the package manager that gives you full codec support
      for each tool. Mix package managers if needed.
```

---

## 4. Video Codecs on Windows

### 4.1 Supported Codecs

| Codec | FFmpeg Encoder | GPU? | Requirement |
|-------|---------------|------|-------------|
| `copy` | — | — | No encode, stream copy |
| `prores` | `libprores` | CPU | ffmpeg with ProRes support |
| `prores_ks` | `prores_ks` | CPU | ffmpeg with ProRes KS |
| `h264` (NVIDIA) | `h264_nvenc` | NVENC | NVIDIA GPU + driver |
| `hevc` (NVIDIA) | `hevc_nvenc` | NVENC | NVIDIA GPU + driver |
| `h264` (Intel) | `h264_qsv` | QSV | Intel GPU + MediaSDK/VPL |
| `hevc` (Intel) | `hevc_qsv` | QSV | Intel GPU + MediaSDK/VPL |

### 4.2 Codecs NOT Available on Windows

These encoders exist on other platforms but are **not available on Windows**:

| Encoder | Platform | Reason |
|---------|----------|--------|
| `aac_at` | macOS only | Uses Apple AudioToolbox |
| `h264_vaapi` | Linux only | VA-API is Linux DRM/Mesa |
| `hevc_vaapi` | Linux only | VA-API is Linux DRM/Mesa |
| `prores_videotoolbox` | macOS only | Uses Apple VideoToolbox |
| `hevc_videotoolbox` | macOS only | Uses Apple VideoToolbox |

`platform_supports_codec()` on Windows must return `0` (unsupported) for all
of the above. Requesting these codecs must produce `ERR_GPU_NOT_SUPPORTED`.

---

## 5. Audio Codecs on Windows

### 5.1 Codec Support Table with Fallback Chain

| Codec | Windows Support | Fallback |
|-------|-----------------|----------|
| `pcm_s16le` | ✅ Always available | — |
| `aac_at` | ❌ macOS only | Not available at all |
| `libfdk_aac` | ✅ If MSYS2 ffmpeg used | → `aac` (native) |
| `aac` | ✅ Always available | — |
| `ac3` | ✅ Always available | — |

**RULE:** The **only** permitted codec fallback is `libfdk_aac → aac`.
No other fallback is allowed. If `libfdk_aac` is unavailable, the converter
falls back to native `aac`. All other codecs must be present or fail with
an error.

### 5.2 Audio Output Modes on Windows

| Mode | Status | Notes |
|------|--------|-------|
| `pcm` | ✅ Available | `pcm_s16le`, always works |
| `fdk_aac_q5` | ✅ Available | Uses `libfdk_aac` if present, else `aac` |
| `fdk_aac_q5_ac3_640` | ✅ Available | Dual audio: AAC + AC3 640k |
| `fdk_aac_q2` | ✅ Available | Uses `libfdk_aac` if present, else `aac` |
| `fdk_aac_q2_ac3_640` | ✅ Available | Dual audio: AAC + AC3 640k |

---

## 6. GPU Acceleration on Windows

### 6.1 NVIDIA NVENC

#### Requirements

```
✓ NVIDIA GPU (GeForce, Quadro, Tesla — any NVENC-capable model)
✓ NVIDIA Display Driver (recent version with NVENC API)
✓ FFmpeg compiled with NVENC support:
    --enable-nonfree --enable-cuda-nvcc --enable-nvenc
  (MSYS2 mingw64 ffmpeg includes NVENC by default when CUDA toolkit present)
```

#### Verification

```bat
ffmpeg -encoders | findstr nvenc
```

Expected output (if NVENC is available):

```
 V..... hevc_nvenc              NVIDIA NVENC HEVC encoder
 V..... h264_nvenc              NVIDIA NVENC H.264 encoder
```

If no output: NVENC not available. Check GPU driver and FFmpeg build flags.

#### Example FFmpeg Command (H.264 NVENC)

```bash
ffmpeg -i input.mov \
  -c:v h264_nvenc -b:v 5M -rc_mode auto \
  -c:a aac -q:a 2 -ar 48000 \
  -af "aresample=resampler=soxr:precision=28:cheby=1" \
  output.mkv
```

#### Example FFmpeg Command (HEVC NVENC)

```bash
ffmpeg -i input.mov \
  -c:v hevc_nvenc -b:v 8M -rc_mode auto \
  -c:a aac -q:a 2 -ar 48000 \
  -af "aresample=resampler=soxr:precision=28:cheby=1" \
  output.mkv
```

#### Binary Resolution for NVENC

- NVENC detection is done by probing `ffmpeg -encoders` at startup
- `nvenc-info.exe` is **not required** and not checked
- Result is cached in platform state after `platform_init()`
- `platform_detect_gpu_support()` returns a bitmask:
  `GPU_NVENC_H264 | GPU_NVENC_HEVC` (as available)

### 6.2 Intel Quick Sync Video (QSV)

#### Requirements

```
✓ Intel GPU (Intel Iris, UHD Graphics, Arc A-series)
✓ Intel Media SDK OR Intel VPL (Video Processing Library) installed
✓ FFmpeg compiled with QSV support:
    --enable-libmfx     (older: Intel Media SDK)
    --enable-libvpl     (newer: Intel VPL)
  (MSYS2 mingw64 ffmpeg includes QSV support if libmfx/libvpl is installed)
```

#### Verification

```bat
ffmpeg -encoders | findstr qsv
```

Expected output (if QSV is available):

```
 V..... h264_qsv                Intel Quick Sync H.264 encoder
 V..... hevc_qsv                Intel Quick Sync HEVC encoder
```

#### Example FFmpeg Command (HEVC QSV)

```bash
ffmpeg -i input.mov \
  -init_hw_device qsv=hw \
  -filter_complex "[0:v]hwupload=extra_hw_frames=64,format=qsv[v]" \
  -map "[v]" -map 0:a:0 \
  -c:v hevc_qsv -q:v 5 \
  -c:a aac -q:a 2 -ar 48000 \
  -af "aresample=resampler=soxr:precision=28:cheby=1" \
  output.mkv
```

#### Example FFmpeg Command (H.264 QSV)

```bash
ffmpeg -i input.mov \
  -init_hw_device qsv=hw \
  -filter_complex "[0:v]hwupload=extra_hw_frames=64,format=qsv[v]" \
  -map "[v]" -map 0:a:0 \
  -c:v h264_qsv -q:v 5 \
  -c:a aac -q:a 2 -ar 48000 \
  -af "aresample=resampler=soxr:precision=28:cheby=1" \
  output.mkv
```

---

## 7. Audio Filters — Mandatory Dependencies

### 7.1 Required FFmpeg Configure Flags

The following FFmpeg build flags are **mandatory** on Windows. Without them,
the converter will refuse to start:

```bash
./configure \
  --enable-filter=aresample \
  --enable-libsoxr \
  --enable-filter=volumedetect \
  --enable-filter=loudnorm \
  --enable-filter=deblock \
  --enable-filter=asplit \
  --enable-filter=format \
  --enable-filter=hwupload \
  --enable-filter=volume \
  ... other flags
```

`--enable-libsoxr` is **CRITICAL** — it enables `resampler=soxr` in the
`aresample` filter. This is a **hard quality requirement**, not a soft
preference: `resampler=soxr` is the only resampler that meets the audio
quality bar required by this converter. There is no fallback to the default
resampler — if `libsoxr` is absent, `platform_validate_audio_filters()`
will return `ERR_AUDIO_FILTER_VALIDATION_FAILED` and the converter will
refuse to start.

MSYS2 mingw64 ffmpeg (`mingw-w64-x86_64-ffmpeg`) includes `libsoxr` by
default. Chocolatey or standalone builds may not.

### 7.2 Validation at Startup

The converter **must** validate all required audio filters when
`platform_init()` is called on Windows:

```
platform_init()
    │
    ├──► Resolve binaries (ffmpeg, ffprobe, mkvmerge, MP4Box)
    │
    └──► windows_validate_audio_filters()
             │
             ├──► Run: ffmpeg -filters 2>nul
             │
             ├──► Check: aresample present?  NO → FAIL
             ├──► Check: volumedetect present? NO → FAIL
             ├──► Check: loudnorm present?   NO → FAIL
             ├──► Check: asplit present?     NO → FAIL
             │
             ├──► Check: libsoxr available?
             │    (run: ffmpeg -af aresample=resampler=soxr -f null -i nul 2>nul)
             │    NO → FAIL
             │
             └──► All present: return 0 (success)
                  Any missing: return ERR_AUDIO_FILTER_VALIDATION_FAILED
```

**RULE:** If **any** required filter is missing → `ERR_AUDIO_FILTER_VALIDATION_FAILED`.
There is **NO fallback** for filters. Only `libfdk_aac → aac` has a fallback.

### 7.3 Audio Filter Chain Examples

#### Peak Normalization Pass 1 (measure max volume)

```bat
ffmpeg -filter_threads 4 -vn -i input.mov ^
  -af volumedetect -f null nul 2>&1 | findstr max_volume
```

#### Peak Normalization Pass 2 (apply gain)

After measuring `max_volume` from pass 1, apply the inverse gain:

```bat
ffmpeg -i input.mov ^
  -af "aresample=resampler=soxr:precision=28:cheby=1,volume={result_dB}dB" ^
  -c:v copy -c:a aac output.mkv
```

Replace `{result_dB}` with the computed gain (e.g., `+3.2dB`).

#### Loudness Normalization Pass 1 (EBU R128 analysis)

```bat
ffmpeg -filter_threads 4 -vn -i input.mov ^
  -af "loudnorm=I=-11:TP=-1.5:LRA=7:linear=true:print_format=json" ^
  -f null nul 2>&1 > analysis.json
```

#### Loudness Normalization Pass 2 (apply measured values)

Example for Rock genre (I=−11, TP=−1.0, LRA=7):

```bat
ffmpeg -i input.mov ^
  -af "aresample=resampler=soxr:precision=28:cheby=1,loudnorm=I=-11:TP=-1.0:LRA=7:measured_I={M_I}:measured_TP={M_TP}:measured_LRA={M_LRA}:measured_thresh={M_thresh}:linear=true" ^
  -c:v copy -c:a aac output.mkv
```

Replace `{M_I}`, `{M_TP}`, `{M_LRA}`, `{M_thresh}` with values from the JSON
produced in pass 1.

#### Dual Audio Track (AAC + AC3)

```bat
ffmpeg -i input.mov ^
  -map 0:v:0 ^
  -filter_complex "[0:a:0]aresample=resampler=soxr:precision=28:cheby=1,asplit=2[aout0][aout1]" ^
  -map [aout0] -map [aout1] ^
  -c:v copy ^
  -c:a:0 libfdk_aac -vbr:a:0 5 -ar:a:0 48000 ^
  -c:a:1 ac3 -b:a:1 640k -ar:a:1 48000 ^
  output.mkv
```

---

## 8. Path Handling Issues & Solutions

### 8.1 Path Escaping for Command Lines

#### Problem

Windows paths frequently contain spaces, parentheses, and special characters:

```
Input:         C:\My Documents\Video (2024).mp4

Without quotes: ffmpeg -i C:\My Documents\Video (2024).mp4
→ Shell splits on space: "C:\My", "Documents\Video", "(2024).mp4" — WRONG

With quotes:   ffmpeg -i "C:\My Documents\Video (2024).mp4"
→ Correct for this case

But path contains a quote:
Input:         C:\My"Video".mp4
Command:       ffmpeg -i "C:\My"Video".mp4"
→ Inner quotes break the command line — WRONG

Correct:       ffmpeg -i "C:\My\"Video\".mp4"
```

Additional special characters that need escaping in `cmd.exe` context:
`&`, `|`, `(`, `)`, `>`, `<`, `^`, `%`

#### Solution: `platform_escape_path_for_command()`

```c
/*
 * Escapes a file path for safe use in a Windows command line passed to
 * popen() or system(). The returned string is heap-allocated; the caller
 * must free() it.
 *
 * Logic:
 *   1. Scan path for double-quote characters.
 *   2. Escape each " as \".
 *   3. Wrap the entire result in double quotes.
 *
 * This is sufficient for popen() calls via MSYS2's runtime, which uses
 * POSIX-style quoting even on Windows.
 */
char* windows_escape_path_for_command(const char* path);
```

#### Windows-Specific Escaping Rules

| Character | Raw | Escaped (within quotes) |
|-----------|-----|------------------------|
| Double quote `"` | `"` | `\"` |
| Backslash `\` | `\` | `\` (no escaping needed inside `"..."`) |
| Percent `%` | `%` | `%%` (batch context only) |
| Ampersand `&` | `&` | Safe inside `"..."` |
| Pipe `\|` | `\|` | Safe inside `"..."` |
| Parentheses `()` | `()` | Safe inside `"..."` |
| Caret `^` | `^` | Safe inside `"..."` |

**Rule:** Wrap the entire path in `"..."` and only escape inner `"` as `\"`.
This covers all practical cases when calling via `popen()` in MSYS2 MinGW.

### 8.2 File Path Separators

#### Problem

Mixing forward and backward slashes causes confusion:

```
Mixed:    C:\Users\User\output/filename_converted.mkv
Some Windows tools (non-MSYS2) reject forward slashes in paths.
```

#### Solution: `platform_join_paths()`

```c
/*
 * Joins a directory path and a filename using the Windows path separator.
 * Returns a heap-allocated string; caller must free().
 *
 * Examples:
 *   platform_join_paths("C:\\Users\\User\\output", "file.mkv")
 *   → "C:\\Users\\User\\output\\file.mkv"
 *
 *   platform_join_paths("C:\\folder\\", "file.mkv")
 *   → "C:\\folder\\file.mkv"   (no double backslash)
 */
char* windows_join_paths(const char* dir, const char* file);
```

Always uses `\` as the separator on Windows. Never mixes `/` and `\`.

### 8.3 Absolute Path Detection

#### Windows Absolute Path Rules

| Path | Absolute? | Reason |
|------|-----------|--------|
| `C:\folder\file.mkv` | ✅ Yes | Drive letter + colon + backslash |
| `D:\output` | ✅ Yes | Drive letter + colon + backslash |
| `\\server\share\file` | ✅ Yes | UNC path |
| `\folder\file` | ❌ No | Relative to current drive root |
| `folder\file` | ❌ No | Relative path |
| `.\folder\file` | ❌ No | Relative to current directory |

#### Solution: `platform_path_is_absolute()`

```c
int windows_path_is_absolute(const char* path) {
    if (!path) return 0;
    /* UNC path: \\server\share */
    if (path[0] == '\\' && path[1] == '\\') return 1;
    /* Drive letter: C:\ or C:/ */
    if (isalpha((unsigned char)path[0]) && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/')) return 1;
    return 0;
}
```

Expected results:

```c
platform_path_is_absolute("C:\\folder")        → 1
platform_path_is_absolute("\\\\server\\share") → 1
platform_path_is_absolute("\\folder")          → 0  /* relative to drive */
platform_path_is_absolute("folder\\file")      → 0
```

---

## 9. Pipe & Output Handling Issues

### 9.1 popen() Line Endings

#### Problem

FFmpeg and other tools on Windows output lines ending with `\r\n` (CRLF),
not `\n` (LF) as on Linux/macOS:

```
Windows popen output:  "out_time_ms=1234567\r\n"
Linux/macOS output:    "out_time_ms=1234567\n"
```

Most `strstr()`-based checks work on both because they search for a
substring. However, any code that:
- Compares the trailing character
- Parses the last field in a line
- Uses `strlen()` to find the end of a value

...will receive an unexpected `\r` before the `\n` and may produce
incorrect results or garbage output.

#### Solution: `platform_normalize_output_line()`

```c
/* Windows implementation: strip trailing \r before \n */
void windows_normalize_output_line(char* line) {
    if (!line) return;
    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') {
        len--;
        if (len > 0 && line[len-1] == '\r') {
            line[len-1] = '\n';
            line[len]   = '\0';
        }
    }
}

/* POSIX implementation: no-op */
void posix_normalize_output_line(char* line) {
    (void)line; /* already \n only */
}
```

`platform_normalize_output_line()` must be called immediately after every
`fgets()` from a `popen()` pipe.

### 9.2 Character Encoding

#### Problem

Windows console codepage is often Windows-1252 (Western European legacy)
or, on Windows 11, UTF-8. FFmpeg outputs UTF-8. File paths containing
non-ASCII characters (Cyrillic, CJK, accented Latin) display incorrectly
or cause parsing errors when the console codepage does not match:

```
File path in UTF-8:   "Видео.mp4"
Displayed in cp1252:  "ÐÐ¸Ð´ÐµÐ¾.mp4"  ← garbled
```

#### Solution: Set Console Codepage to UTF-8 in `platform_init()`

```c
#include <windows.h>

void windows_set_utf8_console(void) {
    SetConsoleCP(65001);        /* Input codepage  → UTF-8 */
    SetConsoleOutputCP(65001);  /* Output codepage → UTF-8 */
}
```

Call `windows_set_utf8_console()` at the start of `windows_platform_init()`.
This ensures FFmpeg output and file paths display correctly in the console.

---

## 10. Home Directory Handling

### 10.1 Environment Variables by Platform

| Platform | Primary | Fallback |
|----------|---------|---------|
| Linux | `$HOME` | `/etc/passwd` entry |
| macOS | `$HOME` | `NSHomeDirectory()` |
| Windows | `%USERPROFILE%` | `%HOMEDRIVE%` + `%HOMEPATH%` |

On Windows, `$HOME` is not set by default. Code that calls `getenv("HOME")`
returns `NULL` on a standard Windows system.

### 10.2 Solution: `platform_get_home_dir()`

```c
const char* windows_get_home_dir(void) {
    /* Primary: USERPROFILE — set by Windows for every user */
    const char* home = getenv("USERPROFILE");
    if (home && home[0] != '\0') return home;

    /* Fallback: combine HOMEDRIVE + HOMEPATH */
    const char* drive = getenv("HOMEDRIVE");
    const char* hpath = getenv("HOMEPATH");
    if (drive && hpath) {
        /* Build and cache: HOMEDRIVE + HOMEPATH */
        static char combined[MAX_PATH];
        snprintf(combined, sizeof(combined), "%s%s", drive, hpath);
        if (combined[0] != '\0') return combined;
    }

    /* Last resort: SHGetKnownFolderPath(FOLDERID_Documents) */
    /* (requires linking shlobj.h / shell32.lib) */

    return NULL; /* ERR_HOME_DIR_NOT_FOUND */
}
```

The result is cached in platform state after first call. Subsequent calls
return the cached value without repeated `getenv()` lookups.

---

## 11. Directory Creation

### 11.1 Problem: `mkdir()` Signature Difference

```
POSIX:   int mkdir(const char* path, mode_t mode);
Windows: int _mkdir(const char* path);   /* No mode parameter! */
```

Calling `mkdir(path, 0755)` does not compile on Windows without POSIX
compatibility shims. Using `_mkdir(path)` does not compile on POSIX.

### 11.2 Path Separator Issue in Recursive mkdir

```
POSIX mkdir_p iterates by '/' and calls mkdir() for each component:
   /Users/user/output  →  mkdir /Users, mkdir /Users/user, mkdir /Users/user/output

Windows path:
   C:\Users\User\output
   ↑ No '/' separators — POSIX mkdir_p fails to split this path correctly
```

### 11.3 Solution: `platform_mkdir_recursive()`

```c
/*
 * Creates a directory and all missing parent components on Windows.
 * Handles both '/' and '\' as path separators.
 * Returns 0 on success, -1 on error.
 */
int windows_mkdir_recursive(const char* path) {
    char tmp[MAX_PATH];
    size_t len;
    char* p;

    if (!path || path[0] == '\0') return -1;
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    len = strlen(tmp);

    /* Strip trailing separator */
    if (len > 0 && (tmp[len-1] == '\\' || tmp[len-1] == '/'))
        tmp[--len] = '\0';

    /* Walk path and create each component */
    for (p = tmp + 1; *p; p++) {
        if (*p == '\\' || *p == '/') {
            *p = '\0';
            /* Skip drive root: "C:" */
            if (!(p == tmp + 2 && tmp[1] == ':')) {
                if (_mkdir(tmp) != 0 && errno != EEXIST)
                    return -1;
            }
            *p = '\\';
        }
    }
    /* Create the final component */
    if (_mkdir(tmp) != 0 && errno != EEXIST)
        return -1;
    return 0;
}
```

---

## 12. Tool Resolution Scenarios

### 12.1 Scenario 1: MSYS2 Full Installation (Recommended)

```
User installed:
  pacman -S mingw-w64-x86_64-ffmpeg
  pacman -S mingw-w64-x86_64-mkvtoolnix
  choco install gpac

Resolution at platform_init():
  ffmpeg:   C:\msys64\mingw64\bin\ffmpeg.exe    ✓ (Priority 3: MSYS2)
  ffprobe:  C:\msys64\mingw64\bin\ffprobe.exe   ✓ (Priority 3: MSYS2)
  mkvmerge: C:\msys64\mingw64\bin\mkvmerge.exe  ✓ (Priority 3: MSYS2)
  MP4Box:   C:\ProgramData\chocolatey\lib\      ✓ (Priority 4: Chocolatey)
            gpac\tools\mp4box.exe

Audio filter validation:
  aresample (soxr): PRESENT ✓
  volumedetect:     PRESENT ✓
  loudnorm:         PRESENT ✓
  asplit:           PRESENT ✓
  libfdk_aac:       PRESENT ✓

Result: FULL FUNCTIONALITY. All features available.
```

### 12.2 Scenario 2: Mixed Installation (Problem Case)

```
User installed:
  choco install ffmpeg          (different build, may lack libfdk-aac/libsoxr)
  pacman -S mingw-w64-x86_64-mkvtoolnix
  Custom GPAC installation at D:\tools\gpac\mp4box.exe

Resolution at platform_init():
  ENV vars: not set
  Bundled:  not present
  MSYS2:    ffmpeg.exe NOT in C:\msys64\mingw64\bin (pacman -S ffmpeg not run)
  Chocolatey: ffmpeg.exe found at C:\ProgramData\chocolatey\bin\ffmpeg.exe

  ffmpeg:   C:\ProgramData\chocolatey\bin\ffmpeg.exe  ✓ (Priority 4)
  ffprobe:  C:\ProgramData\chocolatey\bin\ffprobe.exe ✓ (Priority 4)
  mkvmerge: C:\msys64\mingw64\bin\mkvmerge.exe        ✓ (Priority 3)
  MP4Box:   NOT FOUND at priorities 1–4

  PATH search for MP4Box:
    D:\tools\gpac\ not in PATH → NOT FOUND → ERR_BINARY_NOT_FOUND

Audio filter validation for Chocolatey ffmpeg:
  aresample (soxr): MISSING ✗ → ERR_AUDIO_FILTER_VALIDATION_FAILED

Resolution: FAIL. User must either:
  (a) Set FFMPEG_BIN=C:\msys64\mingw64\bin\ffmpeg.exe (after installing MSYS2 ffmpeg)
  (b) Set MP4BOX_BIN=D:\tools\gpac\mp4box.exe
  (c) Install MSYS2 ffmpeg for full codec support
```

### 12.3 Scenario 3: Environment Variable Override

```
User sets (in cmd.exe or System Environment Variables):
  SET FFMPEG_BIN=C:\my\custom\ffmpeg\ffmpeg.exe
  SET FFPROBE_BIN=C:\my\custom\ffmpeg\ffprobe.exe
  SET MKVMERGE_BIN=C:\tools\mkvtoolnix\mkvmerge.exe
  SET MP4BOX_BIN=C:\tools\gpac\mp4box.exe

Behavior at platform_init():
  Priority 1 (ENV vars): all four env vars set and files exist
  → Uses custom paths immediately
  → Skips MSYS2/Chocolatey/PATH search entirely

This is the recommended approach for:
  - CI/CD environments
  - Portable builds with bundled binaries
  - Development with multiple FFmpeg versions installed
  - Production deployment with pinned binary versions
```

---

## 13. Windows-Specific Error Codes

The following error codes must be added to the `ConverterError` enum in
`src/converter/converter.h`:

```c
typedef enum {
    /* ... existing codes ... */

    /* Windows platform errors */
    ERR_PLATFORM_INIT_FAILED           = 100, /* windows_platform_init() failed */
    ERR_AUDIO_FILTER_VALIDATION_FAILED = 101, /* Required FFmpeg filter not found */
    ERR_GPU_NOT_SUPPORTED              = 102, /* Requested GPU codec unavailable */
    ERR_SUBPROCESS_START_FAILED        = 103, /* popen() failed (replaces ERR_POPEN_FAILED) */
    ERR_SUBPROCESS_CLOSE_FAILED        = 104, /* pclose() failed (replaces ERR_PCLOSE_FAILED) */
    ERR_PATH_TOO_LONG                  = 105, /* Path exceeds MAX_PATH (260 chars on legacy systems) */
    ERR_HOME_DIR_NOT_FOUND             = 106, /* Cannot determine home directory */

} ConverterError;
```

**Note:** `ERR_SUBPROCESS_START_FAILED` replaces `ERR_POPEN_FAILED` and
`ERR_SUBPROCESS_CLOSE_FAILED` replaces `ERR_PCLOSE_FAILED`. This is a
**breaking change** in the public API — callers checking the old codes must
be updated.

**Note on `ERR_PATH_TOO_LONG`:** The classic `MAX_PATH` limit of 260
characters applies to legacy Windows APIs. Windows 10 version 1607 and later
can lift this limit when the `LongPathsEnabled` registry key is set or when
the application manifest declares `longPathAware`. The current implementation
checks against `MAX_PATH` for compatibility with all Windows versions; builds
that target Windows 10+ with long-path awareness enabled can raise this limit.

---

## 14. Windows-Specific Implementation Details

### 14.1 `converter_windows.c` Key Functions

All functions implement the `platform_*()` interface declared in
`src/converter/converter_platform.h`. The build system links exactly one
platform file per build target.

| Windows Function | Platform Interface | Description |
|------------------|--------------------|-------------|
| `windows_platform_init()` | `platform_init()` | Resolve binaries, validate filters, detect GPU |
| `windows_platform_cleanup()` | `platform_cleanup()` | Free cached paths and state |
| `windows_get_exe_dir()` | `platform_get_exe_dir()` | `GetModuleFileNameW()` |
| `windows_get_ffmpeg_bin()` | `platform_get_ffmpeg_bin()` | 6-priority search for ffmpeg.exe |
| `windows_get_ffprobe_bin()` | `platform_get_ffprobe_bin()` | 6-priority search for ffprobe.exe |
| `windows_get_mkvmerge_bin()` | `platform_get_mkvmerge_bin()` | 6-priority search for mkvmerge.exe |
| `windows_get_mp4box_bin()` | `platform_get_mp4box_bin()` | 6-priority search for mp4box.exe |
| `windows_escape_path_for_command()` | `platform_escape_path_for_command()` | Quote and escape path |
| `windows_mkdir_recursive()` | `platform_mkdir_recursive()` | `_mkdir()` for each component |
| `windows_get_home_dir()` | `platform_get_home_dir()` | `USERPROFILE` or `HOMEDRIVE+HOMEPATH` |
| `windows_get_filename()` | `platform_get_filename()` | Find last `\` or `/` in path |
| `windows_join_paths()` | `platform_join_paths()` | Join with `\` separator |
| `windows_path_is_absolute()` | `platform_path_is_absolute()` | Drive letter or UNC check |
| `windows_get_null_device()` | `platform_get_null_device()` | Returns `"nul"` |
| `windows_normalize_output_line()` | `platform_normalize_output_line()` | Strip `\r` before `\n` |
| `windows_validate_audio_filters()` | `platform_validate_audio_filters()` | Probe `ffmpeg -filters` |
| `windows_supports_codec()` | `platform_supports_codec()` | Check codec against allowed list |
| `windows_get_video_codec_flags()` | `platform_get_video_codec_flags()` | Return NVENC/QSV flags |
| `windows_detect_gpu_support()` | `platform_detect_gpu_support()` | Probe `ffmpeg -encoders` |
| `windows_get_cpu_count()` | `platform_get_cpu_count()` | `GetSystemInfo().dwNumberOfProcessors` |
| `windows_get_video_info()` | `platform_get_video_info()` | Returns `0` (not needed on Windows) |

### 14.2 Windows API Headers and Libraries

#### Required Headers

```c
#include "../converter_platform.h"
#include <windows.h>      /* GetModuleFileNameW, GetSystemInfo, SetConsoleCP */
#include <shlwapi.h>      /* PathFindOnPathA, PathFileExistsA */
#include <direct.h>       /* _mkdir */
#include <io.h>           /* _access */
#include <stdlib.h>       /* getenv, malloc, free */
#include <string.h>       /* strlen, strncpy, snprintf */
#include <stdio.h>        /* popen, pclose, fgets, snprintf */
#include <errno.h>        /* EEXIST */
#include <ctype.h>        /* isalpha (for path_is_absolute) */
```

#### Required Link Libraries

```cmake
# In CMakeLists.txt for windows_cli target:
target_link_libraries(windows_cli PRIVATE
    shlwapi    # PathFindOnPathA, PathFileExistsA
    shell32    # SHGetKnownFolderPath (if used for home dir)
)
```

### 14.3 CMakeLists.txt Platform Selection

```cmake
if(WIN32)
    target_sources(windows_cli PRIVATE
        src/converter/platform/converter_windows.c
    )
elseif(APPLE)
    target_sources(macos_cli PRIVATE
        src/converter/platform/converter_macos.c
    )
elseif(UNIX)
    target_sources(linux_cli PRIVATE
        src/converter/platform/converter_linux.c
    )
endif()
```

Only one platform source file is compiled per build. The `platform_*()`
interface ensures that `converter.c` remains identical on all platforms.

### 14.4 Windows Build Instructions

Build from an **MSYS2 MinGW x64** shell (not MSYS shell, not cmd.exe):

```bash
# Install required packages (once)
pacman -S --needed \
    mingw-w64-x86_64-toolchain \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-pkgconf \
    mingw-w64-x86_64-ffmpeg \
    mingw-w64-x86_64-jansson

# Build
mkdir -p build && cd build
cmake -G "MSYS Makefiles" -DCMAKE_PREFIX_PATH=/mingw64 ..
cmake --build . --target windows_cli

# The binary will be at:
# build/windows_cli.exe  (or similar, per CMakeLists.txt)
```

---

*This document describes the Windows implementation of `converter.c` as
planned in `docs/CONVERTER_REFACTORING_PLAN.md` and
`docs/CONVERTER_REFACTORING_CHANGES.md`. Implementation details are based
on the code analysis in `docs/CONVERTER_CODE_ANALYSIS.md`.*
