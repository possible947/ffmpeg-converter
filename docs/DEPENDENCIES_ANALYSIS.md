# Dependencies Analysis — ffmpeg-converter

Complete reference for all libraries, tools, codecs, and filters used by the
**ffmpeg-converter** project (C/CMake path and Free Pascal path).

---

## Table of Contents

1. [System Dependencies by Platform](#1-system-dependencies-by-platform)
2. [FFmpeg Library and Tools](#2-ffmpeg-library-and-tools)
3. [Video Codecs](#3-video-codecs)
4. [Audio Codecs and Codec Selection Chain](#4-audio-codecs-and-codec-selection-chain)
5. [FFmpeg Filters](#5-ffmpeg-filters)
6. [GPU Acceleration](#6-gpu-acceleration)
7. [External Utilities](#7-external-utilities)
8. [Additional Libraries](#8-additional-libraries)
9. [Codec Compatibility Tables](#9-codec-compatibility-tables)
10. [Installation Commands per Platform](#10-installation-commands-per-platform)

---

## 1. System Dependencies by Platform

### Windows — MSYS2 / MinGW-w64

| Package | Role | Required |
|---------|------|----------|
| `mingw-w64-x86_64-toolchain` | GCC compiler, make, binutils | ✅ Yes |
| `mingw-w64-x86_64-cmake` | Build system (≥ 3.16) | ✅ Yes |
| `mingw-w64-x86_64-pkgconf` | pkg-config for library discovery | ✅ Yes |
| `mingw-w64-x86_64-ffmpeg` | FFmpeg runtime and headers | ✅ Yes |
| `mingw-w64-x86_64-jansson` | JSON parsing for loudnorm 2-pass | ✅ Yes |
| `mingw-w64-x86_64-mkvtoolnix` | `mkvmerge` for MKV mux mode | Optional |
| `mingw-w64-x86_64-gpac` | `MP4Box` for Apple M4V creation | Optional |

> **Note:** Use the **MSYS2 MinGW x64** shell (not MSYS2 MSYS). MSYS2 is
> the officially supported Windows build environment for this project.

---

### Linux — Debian / Ubuntu

| Package | Role | Required |
|---------|------|----------|
| `build-essential` | GCC compiler, make | ✅ Yes |
| `cmake` (≥ 3.16) | Build system | ✅ Yes |
| `pkg-config` | Library discovery | ✅ Yes |
| `ffmpeg` | FFmpeg runtime | ✅ Yes |
| `libjansson-dev` | JSON parsing for loudnorm 2-pass | ✅ Yes |
| `libgtk-4-dev` | GTK4 for Linux GUI | GUI only |
| `mkvtoolnix` | `mkvmerge` for MKV mux mode | Optional |
| `gpac` | `MP4Box` for Apple M4V creation | Optional |

### Linux — Fedora / RHEL

| Package | Role | Required |
|---------|------|----------|
| `gcc gcc-c++ make` | C/C++ compiler and make | ✅ Yes |
| `cmake` (≥ 3.16) | Build system | ✅ Yes |
| `pkgconf-pkg-config` | Library discovery | ✅ Yes |
| `ffmpeg` | FFmpeg runtime | ✅ Yes |
| `jansson-devel` | JSON parsing for loudnorm 2-pass | ✅ Yes |
| `gtk4-devel` | GTK4 for Linux GUI | GUI only |
| `mkvtoolnix` | `mkvmerge` for MKV mux mode | Optional |
| `gpac` | `MP4Box` for Apple M4V creation | Optional |

---

### macOS — Homebrew

| Package | Role | Required |
|---------|------|----------|
| `cmake` (≥ 3.16) | Build system | ✅ Yes |
| `jansson` | JSON parsing for loudnorm 2-pass | ✅ Yes |
| `pkg-config` | Library discovery | ✅ Yes |
| `ffmpeg` (optional) | System-level FFmpeg for CLI | CLI only |
| `mkvtoolnix` | `mkvmerge` for mux mode | Optional |

### macOS — MacPorts

| Package | Role | Required |
|---------|------|----------|
| `cmake` | Build system | ✅ Yes |
| `jansson` | JSON parsing for loudnorm 2-pass | ✅ Yes |
| `pkgconfig` | Library discovery | ✅ Yes |
| `ffmpeg8` | Preferred FFmpeg version for CLI | CLI only |
| `gpac` | `MP4Box` for Apple M4V creation | Optional |
| `mkvtoolnix` | `mkvmerge` for mux mode | Optional |
| `fpc` / `lazarus` | Free Pascal compiler / Lazarus IDE | Pascal path |

> The native macOS GUI bundles `ffmpeg` and `ffprobe` inside the `.app`
> bundle (`Contents/Resources/bin/`). No system FFmpeg is required at runtime
> for the GUI app.

---

## 2. FFmpeg Library and Tools

### Version Requirements

| Component | Minimum | Notes |
|-----------|---------|-------|
| FFmpeg | 4.x+ | 5.x/6.x/7.x recommended |
| ffprobe | Same as FFmpeg | Companion tool for metadata probing |
| libavcodec | Bundled with FFmpeg | Video/audio codec library |
| libavfilter | Bundled with FFmpeg | Filter graph library |
| libavformat | Bundled with FFmpeg | Muxers/demuxers |
| libswresample | Bundled with FFmpeg | Audio resampling |
| libsoxr | Optional | Required for `soxr` resampler in `aresample` filter |
| libfdk-aac | Optional | High-quality AAC encoder |

### Required FFmpeg Configure Flags

For full functionality, FFmpeg must be built with:

```
--enable-libsoxr        # aresample=resampler=soxr
--enable-libfdk-aac     # libfdk_aac encoder (optional, non-free)
--enable-nonfree        # required when libfdk-aac is enabled
--enable-gpl            # required for some filters
--enable-vaapi          # Linux VAAPI hardware encode
--enable-videotoolbox   # macOS VideoToolbox hardware encode
--enable-ffnvcodec      # NVIDIA NVENC (Windows/Linux)
--enable-libmfx         # Intel QSV (Windows/Linux)
```

### macOS FFmpeg Resolution Priority (C path)

```
1. /opt/local/bin/ffmpeg8   ← MacPorts FFmpeg 8
2. /opt/local/bin/ffmpeg    ← MacPorts FFmpeg (any version)
3. bundled in .app bundle   ← Contents/Resources/bin/ffmpeg
4. system PATH
```

Environment variable overrides (all platforms):

```bash
export FFMPEG=/path/to/ffmpeg
export FFMPEG_BIN=/path/to/ffmpeg
export FFPROBE=/path/to/ffprobe
export FFPROBE_BIN=/path/to/ffprobe
```

---

## 3. Video Codecs

All video codecs used in the source code (`src/converter/converter.c`,
`fpc/converter/converter_cmd_builder.pas`):

### 3.1 Cross-Platform Codecs

#### `copy`
Stream copy — no re-encode. Used as the default fallback and for mux mode.

```bash
-c:v copy
```

**Available on:** Windows, Linux, macOS

---

#### `prores`
Apple ProRes encoder (FFmpeg native software encoder).

```bash
-c:v prores -profile:v <1|2|3|4>
```

| Profile value | Name | Notes |
|---------------|------|-------|
| 1 | LT (Light) | Smaller files |
| 2 | Standard | Default |
| 3 | HQ (High Quality) | Higher bitrate |
| 4 | 4444 | Highest quality with alpha |

**Available on:** Windows, Linux, macOS

---

#### `prores_ks`
Apple ProRes encoder using the `prores_ks` encoder with explicit profile names.

```bash
-c:v prores_ks -profile:v <lt|standard|hq|4444>
```

| Profile name | Description |
|--------------|-------------|
| `lt` | Light |
| `standard` | Standard (default) |
| `hq` | High Quality |
| `4444` | ProRes 4444 |

**Available on:** Windows, Linux, macOS

---

### 3.2 macOS-Specific Codecs (VideoToolbox)

#### `prores_videotoolbox`
Apple ProRes encoder using Apple's VideoToolbox framework (hardware on Apple
Silicon, software fallback on Intel via `-allow_sw 1`).

```bash
-c:v prores_videotoolbox -profile:v <1|2|3|4> -allow_sw 1
```

**Available on:** macOS only  
**Requires:** VideoToolbox (macOS 10.8+)

---

#### `hevc_videotoolbox`
HEVC (H.265) encoder using Apple's VideoToolbox hardware encoder. Bitrate is
calculated automatically per file using a sub-linear formula:

```
base = 35000 kbps @ 4K (3840×2160) / 24 fps
bitrate = base × (pixels / base_pixels) × (fps / base_fps)^0.75
clamped to [2000, 80000] kbps
```

```bash
-c:v hevc_videotoolbox -b:v <auto_kbps>k -tag:v hvc1 -spatial_aq 1
```

**Available on:** macOS only  
**Requires:** VideoToolbox with HEVC support (macOS 10.13+)

---

### 3.3 Linux Hardware Codecs (VAAPI — Runtime-Detected)

VAAPI codecs are **runtime-probed**. They are exposed in the codec list only
when the system's GPU driver successfully encodes a test frame via the VAAPI
device at `/dev/dri/renderD128` (or another available render node).

Detection probe command (internal):

```bash
ffmpeg -v error -hide_banner \
  -init_hw_device vaapi=va:"/dev/dri/renderD128" \
  -f lavfi -i color=size=1920x1080:rate=1 \
  -frames:v 1 -vf format=nv12,hwupload \
  -c:v <h264_vaapi|hevc_vaapi> -f null -
```

#### `h264_vaapi`
H.264 hardware encoder via VAAPI.

```bash
-vaapi_device "/dev/dri/renderD128" \
-vf "format=nv12,hwupload" \
-c:v h264_vaapi -rc_mode auto
```

**Available on:** Linux only  
**Requires:** Intel or AMD GPU with VAAPI support + Mesa/VA-API drivers

---

#### `hevc_vaapi`
HEVC (H.265) hardware encoder via VAAPI.

```bash
-vaapi_device "/dev/dri/renderD128" \
-vf "format=nv12,hwupload" \
-c:v hevc_vaapi -rc_mode auto
```

**Available on:** Linux only  
**Requires:** Intel (Gen 8+) or AMD GPU with VAAPI HEVC support

---

## 4. Audio Codecs and Codec Selection Chain

### 4.1 Audio Output Modes

| Mode | Description | Audio streams |
|------|-------------|---------------|
| `pcm` | PCM 16-bit LE, 48 kHz | 1 stream |
| `fdk_aac_q5` | AAC VBR quality 5, 48 kHz | 1 stream |
| `fdk_aac_q5_ac3_640` | AAC VBR q5 + AC3 640 kbps, 48 kHz | 2 streams |
| `fdk_aac_q2` | AAC VBR quality 2, 48 kHz | 1 stream |
| `fdk_aac_q2_ac3_640` | AAC VBR q2 + AC3 640 kbps, 48 kHz | 2 streams |

### 4.2 AAC Encoder Selection Chain

The project uses a runtime fallback chain for AAC encoding. At startup,
`ffmpeg -encoders` is queried to detect which encoders are available:

```
aac_at  →  libfdk_aac  →  aac (native)
```

| Priority | Encoder | Platform | Notes |
|----------|---------|---------|-------|
| 1st | `aac_at` | macOS only | Apple AudioToolbox AAC; highest quality |
| 2nd | `libfdk_aac` | All platforms | Requires FFmpeg built with `--enable-libfdk-aac` |
| 3rd | `aac` | All platforms | FFmpeg native AAC encoder; always available |

**Single AAC stream parameters:**

```bash
# aac_at
-c:a aac_at -q:a 2 -ar 48000

# libfdk_aac (VBR quality 5)
-c:a libfdk_aac -vbr 5 -ar 48000

# libfdk_aac (VBR quality 2)
-c:a libfdk_aac -vbr 2 -ar 48000

# native aac
-c:a aac -q:a 2 -ar 48000
```

**Dual audio stream parameters (AAC + AC3):**

```bash
# Stream 0: AAC (using same fallback chain, per-stream flags)
-c:a:0 aac_at -q:a:0 2 -ar:a:0 48000
# or
-c:a:0 libfdk_aac -vbr:a:0 5 -ar:a:0 48000
# or
-c:a:0 aac -q:a:0 2 -ar:a:0 48000

# Stream 1: AC3
-c:a:1 ac3 -b:a:1 640k -ar:a:1 48000
```

### 4.3 PCM Audio

```bash
-c:a pcm_s16le -ar 48000
```

Used when audio output mode is `pcm` or as the default fallback when no
specific audio mode is set.

---

## 5. FFmpeg Filters

### 5.1 `aresample` — Audio Resampling

Used as the base of every audio filter chain. The SoX resampler provides
high-quality sample-rate conversion.

```bash
aresample=resampler=soxr:precision=28:cheby=1
```

| Parameter | Value | Description |
|-----------|-------|-------------|
| `resampler` | `soxr` | Use libsoxr high-quality resampler |
| `precision` | `28` | 28-bit precision (very high quality) |
| `cheby` | `1` | Chebyshev windowing for minimal passband ripple |

**Requires:** FFmpeg built with `--enable-libsoxr`

---

### 5.2 `volume` — Volume Adjustment

Used in peak normalization modes.

```bash
# peak_norm (1-pass): fixed -3 dB
aresample=resampler=soxr:precision=28:cheby=1,volume=-3dB

# peak_norm_2pass: measured gain from ffprobe volumedetect
aresample=resampler=soxr:precision=28:cheby=1,volume=<gain>dB
```

---

### 5.3 `volumedetect` — Peak Level Analysis

Used in the **peak 2-pass** normalization workflow. The first pass measures
the maximum volume level, which is then applied in the second pass.

```bash
# First pass (analysis)
ffmpeg -i input.mov -af "volumedetect" -f null - 2>&1
# Extracts: max_volume: -X.X dB
```

---

### 5.4 `loudnorm` — EBU R128 Loudness Normalization

Implements the EBU R128 loudness standard. Used in loudness normalization modes.

**1-pass (loudness_norm):**

```bash
aresample=resampler=soxr:precision=28:cheby=1,\
loudnorm=I=-11:TP=-1.5:LRA=7
```

**2-pass (loudness_norm_2pass):** First pass measures the file with ffprobe,
second pass applies measured values for linear normalization:

```bash
# First pass: ffprobe with loudnorm filter (JSON output parsed via jansson)
ffmpeg -i input.mov \
  -af "loudnorm=I=-11:TP=-1.5:LRA=7:print_format=json" \
  -f null - 2>&1

# Second pass: apply measured parameters
aresample=resampler=soxr:precision=28:cheby=1,\
loudnorm=I=-11.0:TP=-1.5:LRA=7.0:\
measured_I=<I>:measured_TP=<TP>:measured_LRA=<LRA>:\
measured_thresh=<thresh>:offset=<offset>:linear=true
```

| Parameter | Default | Description |
|-----------|---------|-------------|
| `I` | `-11.0` | Integrated loudness target (LUFS) |
| `TP` | `-1.5` | True peak ceiling (dBTP) |
| `LRA` | `7.0` | Loudness range target (LU) |
| `linear` | `true` | Linear mode (2-pass only) |

**Requires:** `jansson` library for JSON parsing of first-pass output

---

### 5.5 `deblock` — Video Deblocking Filter

Applies a deblocking filter to the encoded video. Not available for hardware
encoders (VideoToolbox, VAAPI).

**Weak deblock:**

```bash
-vf "deblock=filter=weak:block=4:planes=1"
```

**Strong deblock:**

```bash
-vf "deblock=filter=strong:block=4:alpha=0.12:beta=0.07:gamma=0.06:delta=0.05:planes=1"
```

| Parameter | Weak | Strong | Description |
|-----------|------|--------|-------------|
| `filter` | `weak` | `strong` | Filter strength |
| `block` | `4` | `4` | Block size in pixels |
| `alpha` | default | `0.12` | Strong filter alpha |
| `beta` | default | `0.07` | Strong filter beta |
| `gamma` | default | `0.06` | Strong filter gamma |
| `delta` | default | `0.05` | Strong filter delta |
| `planes` | `1` | `1` | Luma plane only |

---

### 5.6 `asplit` — Duplicate Audio Stream

Used in dual audio output modes (`fdk_aac_q5_ac3_640`, `fdk_aac_q2_ac3_640`)
to create two audio outputs from a single input stream.

```bash
-filter_complex "[0:a:0]aresample=resampler=soxr:precision=28:cheby=1,asplit=2[aout0][aout1]"
-map [aout0]   # → AAC stream
-map [aout1]   # → AC3 stream
```

---

### 5.7 `format=nv12` — Pixel Format Conversion for VAAPI

Before uploading frames to the GPU for VAAPI encoding, they must be converted
to NV12 (planar YUV 4:2:0) format.

```bash
-vf "format=nv12,hwupload"
```

---

### 5.8 `hwupload` — Hardware Frame Upload for VAAPI

Uploads decoded frames from system memory to VAAPI GPU memory. Always used
together with `format=nv12` in the VAAPI pipeline.

```bash
-vf "format=nv12,hwupload"
```

Combined VAAPI filter pipeline:

```bash
ffmpeg -vaapi_device "/dev/dri/renderD128" \
  -i input.mov \
  -vf "format=nv12,hwupload" \
  -c:v h264_vaapi -rc_mode auto \
  output.mp4
```

---

### 5.9 Filter Threading

For 2-pass analysis (peak and loudness), the audio filter thread count is set
to half the number of available CPU cores for parallel processing:

```bash
-filter_threads <N>   # N = cpu_count / 2, minimum 1
```

---

## 6. GPU Acceleration

### 6.1 VAAPI (Video Acceleration API) — Linux

| Property | Details |
|----------|---------|
| Platform | Linux only |
| Codecs | `h264_vaapi`, `hevc_vaapi` |
| Detection | Runtime probe at startup |
| Device path | `/dev/dri/renderD128` (auto-detected) |
| Drivers | Mesa (Intel/AMD), NVIDIA proprietary |
| FFmpeg flag | `--enable-vaapi` |

**Hardware requirements:**

| GPU | H.264 VAAPI | HEVC VAAPI |
|-----|-------------|------------|
| Intel HD 4000+ (Gen 7+) | ✅ | ❌ |
| Intel HD 5500+ (Gen 8+) | ✅ | ✅ |
| Intel UHD 630+ (Gen 9+) | ✅ | ✅ |
| AMD Radeon (GCN 1.0+) | ✅ | ✅ (GCN 2.0+) |
| NVIDIA (via nouveau) | Limited | Limited |

**Required packages (Debian/Ubuntu):**

```bash
sudo apt install -y \
  vainfo \
  libva-dev \
  i965-va-driver        # Intel Gen 6–9
  # or
  intel-media-va-driver  # Intel Gen 9+ (recommended)
  # or
  mesa-va-drivers        # AMD
```

**Verify VAAPI availability:**

```bash
vainfo                                    # list available VAAPI profiles
ffmpeg -hwaccels                          # list FFmpeg hardware accelerators
ffmpeg -encoders | grep -E "h264_vaapi|hevc_vaapi"
```

---

### 6.2 VideoToolbox — macOS

| Property | Details |
|----------|---------|
| Platform | macOS only |
| Codecs | `prores_videotoolbox`, `hevc_videotoolbox` |
| Detection | Always available on macOS 10.8+ |
| Hardware | Apple Silicon (hardware), Intel Mac (software fallback) |
| FFmpeg flag | `--enable-videotoolbox` |

**Hardware requirements:**

| Platform | ProRes VT | HEVC VT |
|----------|-----------|---------|
| Apple Silicon (M1+) | ✅ Hardware | ✅ Hardware |
| Intel Mac (2017+) | ✅ Software via `-allow_sw 1` | ✅ Hardware |
| Intel Mac (pre-2017) | ✅ Software via `-allow_sw 1` | ✅ Hardware (macOS 10.13+) |

---

### 6.3 NVENC — NVIDIA (Windows / Linux)

| Property | Details |
|----------|---------|
| Platform | Windows, Linux |
| Codecs | `h264_nvenc`, `hevc_nvenc` (not used in current source, available in FFmpeg) |
| Drivers | NVIDIA driver 418.30+ |
| SDK | NVIDIA Video Codec SDK |
| FFmpeg flag | `--enable-ffnvcodec` |

**Required packages (Windows MSYS2):**

```bash
pacman -S mingw-w64-x86_64-ffmpeg  # includes NVENC if built with it
```

**Verify NVENC availability:**

```bash
ffmpeg -encoders | grep -E "h264_nvenc|hevc_nvenc"
ffmpeg -hwaccels | grep cuda
```

---

### 6.4 QSV (Intel Quick Sync Video) — Windows / Linux

| Property | Details |
|----------|---------|
| Platform | Windows, Linux |
| Codecs | `h264_qsv`, `hevc_qsv` (not used in current source, available in FFmpeg) |
| Drivers | Intel GPU driver + Intel Media SDK / oneVPL |
| FFmpeg flag | `--enable-libmfx` or `--enable-qsv` |

**Required packages (Windows MSYS2):**

```bash
pacman -S mingw-w64-x86_64-intel-mediasdk  # if available
# or use a pre-built FFmpeg with QSV support
```

**Verify QSV availability:**

```bash
ffmpeg -encoders | grep -E "h264_qsv|hevc_qsv"
ffmpeg -hwaccels | grep qsv
```

---

## 7. External Utilities

### 7.1 `ffmpeg`

Core video/audio conversion tool. Required on all platforms.

| Platform | Search order |
|----------|-------------|
| Linux | Bundled (exe dir) → `FFMPEG`/`FFMPEG_BIN` env → `PATH` |
| macOS CLI | `FFMPEG`/`FFMPEG_BIN` env → `/opt/local/bin/ffmpeg8` → `/opt/local/bin/ffmpeg` → `PATH` |
| macOS GUI | Bundled in `.app/Contents/Resources/bin/ffmpeg` |
| Windows | `FFMPEG`/`FFMPEG_BIN` env → `PATH` |

---

### 7.2 `ffprobe`

Metadata and duration probing tool. Required on all platforms. Used for:
- Duration detection (for progress reporting)
- Video stream info (width, height, fps) for HEVC VT bitrate calculation
- First-pass loudnorm analysis (JSON output)
- First-pass peak volumedetect analysis

| Platform | Search order |
|----------|-------------|
| Linux | Bundled (exe dir) → `FFPROBE`/`FFPROBE_BIN` env → `PATH` |
| macOS GUI | Bundled in `.app/Contents/Resources/bin/ffprobe` |
| Windows | `FFPROBE`/`FFPROBE_BIN` env → `PATH` |

---

### 7.3 `mkvmerge`

MKV container muxing tool from MKVToolNix. Required only for:
- Linux MKV mux mode (`codec=mux`) — replaces video track in existing MKV
- macOS native GUI mux mode

| Platform | Search order |
|----------|-------------|
| Linux | `MKVMERGE_BIN` env → bundled (exe dir) → `PATH` |
| macOS GUI | Bundled in `.app/Contents/Resources/bin/mkvmerge` (optional) → `MKVMERGE_BIN` env → `PATH` |
| Windows | `MKVMERGE_BIN` env → `PATH` |

**Install:**

```bash
# Linux (Debian/Ubuntu)
sudo apt install mkvtoolnix

# macOS (MacPorts)
sudo port install mkvtoolnix

# macOS (Homebrew)
brew install mkvtoolnix

# Windows (MSYS2)
pacman -S mingw-w64-x86_64-mkvtoolnix
```

---

### 7.4 `MP4Box` (GPAC)

Apple M4V packaging tool. Required only for Apple M4V creation workflow.
Used to mux video + AAC + AC3 tracks into an `.m4v` container.

| Platform | Search order |
|----------|-------------|
| Linux | `MP4BOX_BIN` env → bundled (exe dir) → `PATH` |
| macOS GUI | Bundled in `.app/Contents/Resources/bin/MP4Box` + dependent dylibs in `lib/` |
| Windows | `MP4BOX_BIN` env → `PATH` |

**Install:**

```bash
# Linux (Debian/Ubuntu)
sudo apt install gpac

# macOS (MacPorts)
sudo port install gpac

# macOS (Homebrew)
brew install gpac

# Windows (MSYS2)
pacman -S mingw-w64-x86_64-gpac
```

> On Linux, `MP4Box` is staged as a single binary only. No shared-library
> bundle is created, so the target system must have compatible GPAC/runtime
> libraries installed.

---

## 8. Additional Libraries

### 8.1 `jansson`

JSON parsing library. Required for the **loudness normalization 2-pass**
workflow. The first pass produces JSON output from `ffmpeg`'s loudnorm filter
which is parsed to extract measured loudness parameters for the second pass.

| Property | Details |
|----------|---------|
| Version | Any recent (2.x+) |
| License | MIT |
| Headers | Vendored in `third_party/jansson/` (project-local headers) |
| Library | Linked from system (`libjansson`) |
| Homepage | https://digip.org/jansson/ |

**CMake integration:**

```cmake
# third_party/jansson/ provides vendored headers
add_subdirectory(third_party/jansson)
# system libjansson is linked at build time
```

---

### 8.2 `libfdk-aac`

Fraunhofer FDK AAC encoder. Provides higher quality AAC encoding compared to
FFmpeg's native AAC encoder.

| Property | Details |
|----------|---------|
| License | Non-free (requires `--enable-nonfree` in FFmpeg) |
| FFmpeg flag | `--enable-libfdk-aac --enable-nonfree` |
| VBR modes | 1–5 (higher = better quality) |
| Project usage | VBR 2 (`fdk_aac_q2`) or VBR 5 (`fdk_aac_q5`) |
| Priority | 2nd in AAC fallback chain (after `aac_at`) |
| Homepage | https://github.com/mstorsjo/fdk-aac |

**Install (system-level, if building FFmpeg from source):**

```bash
# Linux (Debian/Ubuntu)
sudo apt install libfdk-aac-dev

# macOS (MacPorts)
sudo port install libfdk-aac

# macOS (Homebrew)
brew install fdk-aac
```

---

### 8.3 `libsoxr`

SoX Resampler Library. Required for the `aresample=resampler=soxr` filter
used in all audio processing paths.

| Property | Details |
|----------|---------|
| License | LGPL 2.1+ |
| FFmpeg flag | `--enable-libsoxr` |
| Usage | High-quality audio resampling to 48 kHz |
| Homepage | https://sourceforge.net/projects/soxr/ |

**Install:**

```bash
# Linux (Debian/Ubuntu)
sudo apt install libsoxr-dev

# macOS (MacPorts)
sudo port install libsoxr

# macOS (Homebrew)
brew install libsoxr

# Windows (MSYS2) — bundled with mingw-w64-x86_64-ffmpeg
```

---

### 8.4 `libgtk-4` — GTK4 (Linux GUI only)

GTK4 widget toolkit. Required only for the Linux GTK4 GUI build target.

| Property | Details |
|----------|---------|
| License | LGPL 2.1+ |
| Build target | `linux_gui` only |
| Homepage | https://www.gtk.org/ |

```bash
# Debian/Ubuntu
sudo apt install libgtk-4-dev

# Fedora
sudo dnf install gtk4-devel
```

---

### 8.5 `GPAC / libgpac`

GPAC framework providing `MP4Box`. Required only for Apple M4V creation.

| Property | Details |
|----------|---------|
| License | LGPL 2.1+ |
| Usage | Muxes video, AAC, and AC3 tracks into `.m4v` container |
| Homepage | https://gpac.io/ |

---

## 9. Codec Compatibility Tables

### 9.1 Video Codec Availability by Platform

| Codec | Windows | Linux | macOS | Hardware |
|-------|---------|-------|-------|----------|
| `copy` | ✅ | ✅ | ✅ | No |
| `prores` | ✅ | ✅ | ✅ | No (software) |
| `prores_ks` | ✅ | ✅ | ✅ | No (software) |
| `prores_videotoolbox` | ❌ | ❌ | ✅ | Yes (Apple Silicon) / SW fallback (Intel) |
| `hevc_videotoolbox` | ❌ | ❌ | ✅ | Yes (macOS 10.13+) |
| `h264_vaapi` | ❌ | ✅ (runtime probe) | ❌ | Yes (Intel/AMD GPU) |
| `hevc_vaapi` | ❌ | ✅ (runtime probe) | ❌ | Yes (Intel Gen 8+/AMD) |

### 9.2 Audio Codec Availability by Platform

| Codec | Windows | Linux | macOS | Notes |
|-------|---------|-------|-------|-------|
| `pcm_s16le` | ✅ | ✅ | ✅ | Always available |
| `aac` (native) | ✅ | ✅ | ✅ | Always available in FFmpeg |
| `aac_at` | ❌ | ❌ | ✅ | macOS Audio Toolbox only |
| `libfdk_aac` | ✅* | ✅* | ✅* | Requires `--enable-libfdk-aac` |
| `ac3` | ✅ | ✅ | ✅ | Always available in FFmpeg |

*If FFmpeg was built with libfdk-aac support.

### 9.3 Audio Normalization Mode Compatibility

| Mode | All Platforms | Notes |
|------|---------------|-------|
| `none` | ✅ | `aresample` only |
| `peak_norm` | ✅ | Fixed `-3 dB` |
| `peak_norm_2pass` | ✅ | Requires `volumedetect` filter (1st pass) |
| `loudness_norm` | ✅ | Single-pass `loudnorm` |
| `loudness_norm_2pass` | ✅ | Requires `jansson` for JSON parsing |

### 9.4 GPU Acceleration by Platform

| Technology | Windows | Linux | macOS | GPU Requirement |
|------------|---------|-------|-------|----------------|
| VAAPI | ❌ | ✅ | ❌ | Intel/AMD GPU |
| VideoToolbox | ❌ | ❌ | ✅ | Any Mac (10.8+) |
| NVENC | ✅* | ✅* | ❌ | NVIDIA GPU |
| QSV | ✅* | ✅* | ❌ | Intel GPU |

*Available in FFmpeg but not currently exposed as named codecs in the project
source code. Can be used with `-c:v h264_nvenc`, `-c:v hevc_nvenc`,
`-c:v h264_qsv`, `-c:v hevc_qsv` directly via FFmpeg.

---

## 10. Installation Commands per Platform

### Windows (MSYS2 MinGW x64)

```bash
# Step 1: Update MSYS2
pacman -Syu

# Step 2: Install core build dependencies
pacman -S --needed \
  mingw-w64-x86_64-toolchain \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-jansson \
  mingw-w64-x86_64-ffmpeg

# Step 3 (optional): MKV mux support
pacman -S mingw-w64-x86_64-mkvtoolnix

# Step 4 (optional): M4V creator support
pacman -S mingw-w64-x86_64-gpac

# Step 5: Build
mkdir -p build && cd build
cmake -G "MSYS Makefiles" -DCMAKE_PREFIX_PATH=/mingw64 ..
cmake --build . --target windows_cli

# Step 6: Verify codec support
ffmpeg -encoders | grep -E "qsv|nvenc|h264|hevc"
ffmpeg -hwaccels
```

---

### Linux — Debian / Ubuntu

```bash
# Step 1: Install build dependencies
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  ffmpeg \
  libjansson-dev

# Step 2 (optional): Linux GUI
sudo apt install -y libgtk-4-dev

# Step 3 (optional): MKV mux support
sudo apt install -y mkvtoolnix

# Step 4 (optional): M4V creator support
sudo apt install -y gpac

# Step 5 (optional): VAAPI hardware encoding
sudo apt install -y vainfo libva-dev intel-media-va-driver  # Intel
# or
sudo apt install -y mesa-va-drivers  # AMD

# Step 6: Build
mkdir -p build && cd build
cmake ..
cmake --build . --target linux_cli
cmake --build . --target linux_gui  # if GTK4 installed

# Step 7: Verify VAAPI support
vainfo
ffmpeg -encoders | grep vaapi
```

---

### Linux — Fedora / RHEL

```bash
# Step 1: Install build dependencies
sudo dnf install -y \
  gcc gcc-c++ make \
  cmake \
  pkgconf-pkg-config \
  ffmpeg \
  jansson-devel

# Step 2 (optional): Linux GUI
sudo dnf install -y gtk4-devel

# Step 3 (optional): External tools
sudo dnf install -y mkvtoolnix gpac

# Step 4: Build
mkdir -p build && cd build
cmake ..
cmake --build . --target linux_cli
```

---

### macOS — MacPorts

```bash
# Step 1: Install build dependencies
sudo port install cmake jansson pkgconfig

# Step 2: Install FFmpeg (preferred: version 8)
sudo port install ffmpeg8
# or
sudo port install ffmpeg

# Step 3 (optional): External tools
sudo port install mkvtoolnix gpac

# Step 4 (Pascal path only)
sudo port install fpc lazarus

# Step 5: Place bundled ffmpeg/ffprobe for GUI bundle
# (copy static binaries to src/platform/macos/bin/)

# Step 6: Build
cmake -B build
cmake --build build --target macos_cli
cmake --build build --target macos_gui_native
cmake --install build  # produces build/install/ffmpeg_converter_gui_macos.app

# Step 7: Verify VideoToolbox
ffmpeg -encoders | grep videotoolbox
ffmpeg -hwaccels
```

---

### macOS — Homebrew

```bash
# Step 1: Install build dependencies
brew install cmake jansson pkg-config

# Step 2 (optional): External tools
brew install mkvtoolnix gpac fdk-aac

# Step 3: Build
cmake -B build
cmake --build build --target macos_cli
cmake --build build --target macos_gui_native
cmake --install build
```

---

## Checking FFmpeg Capabilities

Regardless of platform, use these commands to verify what is available in your
local FFmpeg build:

```bash
# List all available encoders
ffmpeg -encoders

# Check specific codec availability
ffmpeg -encoders | grep -E "aac|prores|hevc|h264|vaapi|videotoolbox|nvenc|qsv"

# List hardware accelerators
ffmpeg -hwaccels

# Show FFmpeg build configuration
ffmpeg -buildconf

# Check libsoxr support
ffmpeg -filters | grep aresample
ffmpeg -buildconf | grep soxr

# Check libfdk-aac support
ffmpeg -buildconf | grep fdk
```
