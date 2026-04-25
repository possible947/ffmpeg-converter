# Windows Install and Build

This document covers Windows install/build for both project paths:
- C/CMake (`src/`) using MSVC (Visual Studio Build Tools)
- Free Pascal (`fpc/`)

## 1. C/CMake Path (MSVC only, recommended)

Install Visual Studio 2022 Build Tools (or Visual Studio 2022 with C++ workload).

### 1.1 Install dependencies
- Install Visual Studio Build Tools with MSVC (x64) and CMake tools.
- `ffmpeg`/`ffprobe` are **not** taken from system PATH for this build flow.

### 1.2 Prepare bundled ffmpeg payload
Before configuring/building, place Windows binaries in:

```text
src/platform/windows/bin/
  ffmpeg.exe
  ffprobe.exe
  *.dll (all runtime dependencies required by ffmpeg/ffprobe)
```

Rules:
- Keep all dependent DLLs in the same `bin` folder.
- Do not rely on system-installed ffmpeg for the C/MSVC target.
- `mkvmerge` and `MP4Box` may still be provided through system PATH at runtime.

**AV1 input decoding requirement:**
To decode AV1 source files, `ffmpeg.exe` must be compiled with `--enable-libdav1d` and
`libdav1d-7.dll` must be present in the `bin/` folder alongside `ffmpeg.exe`.
The default bundled binary set in this repository already includes both.
Build `ffmpeg` in MSYS2 ucrt64 with at minimum:
```bash
./configure --enable-libdav1d ...other-flags...
```
Then copy `libdav1d-7.dll` from `/ucrt64/bin/` into `src/platform/windows/bin/`.

### 1.3 Build target

#### Option A — Build script (recommended)

The repo includes a PowerShell build script that automatically locates Visual
Studio, detects the correct CMake generator, and drives the build. It is
compatible with **PowerShell 5.1** (Windows built-in) and later.

```powershell
# Normal incremental build
.\scripts\windows_build.ps1

# Clean build — deletes build-msvc/ and reconfigures from scratch
.\scripts\windows_build.ps1 -Clean

# Debug build
.\scripts\windows_build.ps1 -Config Debug

# Force full recompile of all sources (no CMake reconfigure)
.\scripts\windows_build.ps1 -Rebuild

# Build all targets (not just windows_cli)
.\scripts\windows_build.ps1 -Target ALL_BUILD

# Show all options
.\scripts\windows_build.ps1 -Help
```

A thin CMD launcher is also provided for use from the Windows Explorer or
Command Prompt:

```bat
scripts\windows_build.bat
scripts\windows_build.bat -Clean
scripts\windows_build.bat -Config Debug
```

The script:
- Finds `vswhere.exe` to locate the Visual Studio install path and MSBuild.
- Auto-detects the CMake generator string from the installed VS version
  (e.g. `Visual Studio 17 2022` for VS 2022, `Visual Studio 18 2025` for VS 2025).
- Falls back to reading the generator from an existing `build-msvc/CMakeCache.txt`.
- Skips CMake configuration when `build-msvc/CMakeCache.txt` already exists.
- Finds `cmake.exe` on PATH or falls back to the copy bundled inside VS.

#### Option B — Manual build

From repository root in **x64 Native Tools Command Prompt for VS 2022**:
```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --target windows_cli --config Release
```

Optional debug build:

```powershell
cmake --build build-msvc --target windows_cli --config Debug
```

### 1.4 Output layout
Release output folder:

```text
build-msvc/src/cli/Release/
  ffmpeg_converter.exe
  ffmpeg.exe
  ffprobe.exe
  *.dll (copied from src/platform/windows/bin)
```

## 2. Windows Codec Support

The following video codecs are available on Windows:

| Codec | Type | Requirement |
|---|---|---|
| `copy` | Passthrough | Always available |
| `prores` | Software CPU | Always available |
| `prores_ks` | Software CPU | Always available |
| `h264_nvenc` | NVIDIA GPU | NVIDIA GPU + driver |
| `hevc_nvenc` | NVIDIA GPU | NVIDIA GPU + driver |
| `h264_amf` | AMD GPU | AMD GPU + AMF runtime |
| `hevc_amf` | AMD GPU | AMD GPU + AMF runtime |
| `h264_qsv` | Intel GPU | Intel GPU + MFX runtime |
| `hevc_qsv` | Intel GPU | Intel GPU + MFX runtime |
| `prores_ks_vulkan` | Any GPU (Vulkan) | Vulkan 1.1 driver (any vendor) |
| `mux` | MKV post-mux | `mkvmerge` found on PATH or next to exe |
| `m4v` | Apple M4V creator | `MP4Box` (GPAC) found on PATH or next to exe |

### prores_ks_vulkan

`prores_ks_vulkan` is a GPU-accelerated ProRes encoder that works on **any GPU** with a Vulkan 1.1+ driver (NVIDIA, AMD, Intel). It encodes in ProRes HQ profile (`-profile:v hq`).

The ffmpeg command pipeline it uses:
```
-init_hw_device vulkan=vk:0 -filter_hw_device vk
-i "input"
-vf "format=yuv422p10le,hwupload"
-c:v prores_ks_vulkan -profile:v hq
```

Requirements:
- GPU with Vulkan 1.1 support (all modern GPUs from 2017+)
- `vulkan-1.dll` present in the `bin/` folder (already included in the DLL set)

The codec is automatically available in the menu when `prores_ks_vulkan` appears in `ffmpeg -encoders` output.

### AV1 Input Decoding

When a source file contains an AV1 video stream, the converter automatically selects
an appropriate decoder instead of the native `av1` decoder.

**Why this is necessary:** FFmpeg's native `av1` decoder internally negotiates NVDEC
pixel formats even when no explicit hardware acceleration is requested. NVIDIA GPUs that
do not support AV1 hardware decoding (most GPUs before RTX 30 series) return an error:
`"Failed to get pixel format"`, which terminates the conversion.

**Decoder selection priority (automatic, no user configuration required):**

| Priority | Decoder | Condition |
|---|---|---|
| 1 | `av1_qsv` | Intel QSV encoder detected (Intel Arc / UHD 770+) |
| 2 | `libdav1d` | `libdav1d` decoder present in `ffmpeg -decoders` output |
| 3 | `av1` + `-hwaccel none` | Fallback when neither of the above is available |

**Requirements:**
- `ffmpeg.exe` must be compiled with `--enable-libdav1d` for the `libdav1d` path.
- `libdav1d-7.dll` must be present in the `bin/` folder (already included in the
  default bundled binary set).
- For `av1_qsv`: Intel Arc or 12th-gen+ Intel CPU with integrated graphics and
  the Intel oneVPL/MFX runtime. Detected automatically at startup.

**MSYS2 ucrt64 build flags required:**
```bash
./configure --enable-libdav1d --enable-libvpl  # for libdav1d + QSV
```

### mux

`mux` mode remuxes audio from the source file via ffmpeg copy, then replaces the
video track with an external raw video file using `mkvmerge`, producing an `.mkv`
output. This is useful for pairing a separately encoded video stream with the
original (or processed) audio without re-encoding either.

**Requirements:**
- `mkvmerge` (from [MKVToolNix](https://mkvtoolnix.download/)) must be available.
  Install via any of the following — the converter finds it automatically:
  - **Chocolatey**: `choco install mkvtoolnix`
  - **MSYS2**: `pacman -S mingw-w64-x86_64-mkvtoolnix`
  - **Bundled**: place `mkvmerge.exe` next to `ffmpeg_converter.exe` or in a `bin/` subfolder.
  - **Custom path**: set the `MKVMERGE` or `MKVMERGE_BIN` environment variable to the full path.
- `mux` appears in the codec list **only when** `mkvmerge` is detected at startup.
  If it is not found, the option is silently omitted.

**Usage (command line):**
```powershell
ffmpeg_converter.exe -c mux --video-track replacement.hevc input.mkv
```

**Usage (interactive menu):**
Select `mux` from the codec list, then provide the path to the replacement
video-track file when prompted.

**Notes:**
- Accepts exactly **one source file** per run.
- The replacement video track must be a raw elementary stream
  (`.hevc`, `.h265`, `.h264`, `.264`) or a container readable by mkvmerge.
- Output filename matches the source filename with an `.mkv` extension,
  placed in the configured output directory (default: `%USERPROFILE%\ffmpeg_converter`).
- Frame-rate timing is probed automatically via `ffprobe` and injected into
  the mkvmerge command for H.264/HEVC elementary streams.

### m4v

`m4v` mode produces an Apple M4V container file (`.m4v`) suitable for playback
on Apple TV and other Apple devices. The output contains a passthrough video
track (h264, HEVC, or ProRes) plus two audio tracks — AAC (high-quality,
encoded with `libfdk_aac`) and AC3 (Dolby Digital, encoded with the native
ffmpeg AC3 encoder).

**Requirements:**
- `MP4Box` from [GPAC](https://gpac.io/) must be available on the system.
  Install via any of the following — the converter finds it automatically:
  - **Chocolatey**: `choco install gpac`
  - **Bundled**: place `MP4Box.exe` next to `ffmpeg_converter.exe`.
  - **Custom path**: set the `MP4BOX` or `MP4BOX_BIN` environment variable to
    the full path of `MP4Box.exe`.
- The bundled `ffmpeg.exe` must have been built with `--enable-libfdk_aac`.
  The default binary set in `src/platform/windows/bin/` satisfies this
  (it includes `libfdk-aac-2.dll`).
- Input video codec must be `h264`, `hevc`, `prores`, or `prores_ks`.
- `m4v` appears in the codec list **only when** `MP4Box` is detected at startup.

**M4V-specific options (command line):**

| Option | Description | Default |
|---|---|---|
| `--m4v-video-track <N>` | 0-based video stream index | `0` |
| `--m4v-audio-track <N>` | 0-based audio stream index | `0` |
| `--m4v-aac-quality <1-5>` | libfdk_aac VBR quality | `5` (highest) |
| `--m4v-ac3-bitrate <kbps>` | AC3 bitrate in kbps | `640` |
| `--m4v-lang <tag>` | ISO 639 audio language tag | `rus` |
| `--m4v-chapters` | Embed chapter markers | enabled by default |
| `--no-m4v-chapters` | Disable chapter markers | — |

**Usage (command line):**
```powershell
# Minimal — use all defaults
ffmpeg_converter.exe -c m4v input.mov

# Custom language and AC3 bitrate
ffmpeg_converter.exe -c m4v --m4v-lang eng --m4v-ac3-bitrate 448 input.mov

# Multiple files with output directory
ffmpeg_converter.exe -c m4v -o D:\output --m4v-aac-quality 4 a.mov b.mov
```

**Usage (interactive menu):**
Select `m4v` from the codec list. The menu skips the audio normalisation and
audio output steps (not applicable) and instead prompts for:
1. AAC quality (1–5)
2. AC3 bitrate (384 / 448 / 640 kbps)
3. Audio language tag
4. Chapter embedding (yes/no)
5. Video and audio stream indices

**Pipeline (5 steps):**
1. Extract video track to a temporary `.mp4` (stream copy, no re-encode).
2. Encode AAC audio to a temporary `.m4a` using `libfdk_aac`.
3. Encode AC3 audio to a temporary `.mp4`.
4. Mux all tracks into the final `.m4v` using `MP4Box`.
5. Embed chapter markers (optional, from the source container).

**Notes:**
- Output filename uses the source basename with an `.m4v` extension, placed in
  the configured output directory (default: `%USERPROFILE%\ffmpeg_converter`).
- Both `--overwrite` and the `-o` / `--output` flags work as with other codecs.
- The `--audio-norm`, `--audio-output`, `--profile`, `--deblock`, and `--genre`
  flags are ignored when `-c m4v` is used (m4v manages its own audio pipeline).

## 3. Free Pascal Path

### 2.1 Install dependencies
Install FPC. Install Lazarus too if GUI work is needed.

### 2.2 Build targets
From repository root:
```bash
make -C fpc/build cli
make -C fpc/build tests
```

Notes:
- `fpc/build/Makefile` includes a Windows shared-library output path (`fpc/converter/converter_pas.dll`).

## 3. GUI Notes
If Lazarus GUI build reports missing `Interfaces` or `Forms`, install full Lazarus/LCL widgetset packages for the active compiler/toolchain.

## 4. CLI Behavior Notes
- C CLI uses positional inputs: `ffmpeg_converter [options] file1 file2 ...`.
- `-o/--output` expects output directory path.
- If output directory is not set, default output directory is
	`%USERPROFILE%\ffmpeg_converter` on Windows.
- Mux mode (`-c mux`) requires exactly one input file and a `--video-track` path.
- `mux` only appears in the codec list and is accepted as a `-c` argument when
  `mkvmerge` is detected at startup.
- Apple M4V mode (`-c m4v`) requires `MP4Box` (GPAC) and the bundled ffmpeg
  must include `libfdk_aac` support. `m4v` only appears in the codec list when
  `MP4Box` is detected at startup.

## 5. CI/Release Notes
See `WINDOWS_BRANCH.md` for Windows release/tag workflow details in GitHub Actions.
