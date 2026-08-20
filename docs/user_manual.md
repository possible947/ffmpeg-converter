# ffmpeg_converter User Manual

A specialized media conversion tool for high-quality audio preparation and professional media workflows. ffmpeg_converter simplifies working with `ffmpeg 8.1` by providing intuitive command-line and graphical interfaces. **Important:** This tool requires ffmpeg and ffprobe version 8.1 specifically compiled with **fdk_aac** codec and **soxr** filter support. These tools must be bundled with project binaries—system ffmpeg will not work. This manual covers how to use ffmpeg_converter on macOS, Linux, and Windows.

---

## Table of Contents

1. [What is ffmpeg_converter?](#what-is-ffmpeg_converter)
2. [macOS Usage](#macos-usage)
3. [Linux Usage](#linux-usage)
4. [Windows Usage](#windows-usage)
5. [Common Workflows](#common-workflows)
6. [Codec Reference](#codec-reference)
7. [Troubleshooting](#troubleshooting)

---

## What is ffmpeg_converter?

ffmpeg_converter is a specialized tool designed for **high-quality audio preparation and professional media workflows**. It simplifies working with `ffmpeg 8.1` through intuitive command-line and graphical interfaces.

### Core Purpose

ffmpeg_converter enables professional audio preparation workflows with:

- **High-quality audio codecs** including fdk_aac (FDK AAC encoder) for superior audio quality
- **Advanced audio processing** with EBU R128 loudness normalization and soxr resampling
- **Professional video codecs** (ProRes, H.264, H.265, GPU-accelerated encoders)
- **Audio normalization** (peak/loudness) optimized for different content genres
- **Mux workflows** for remuxing video and audio tracks
- **Apple M4V creator** for producing files compatible with Apple TV
- **Batch processing** for converting multiple files
- **Real-time progress display** with encoding feedback

### Critical Requirements

**ffmpeg_converter requires ffmpeg and ffprobe version 8.1 specifically:**
- Must be compiled with **FDK AAC (fdk_aac)** codec support
- Must include **soxr** (SoX resampler) filter support
- Must be bundled in the project binary directory
- System ffmpeg/ffprobe will NOT work

Without these components, audio workflows will fail or produce degraded quality.

### Key Concepts

- **Input files**: One or more media files you want to convert (positional arguments)
- **Output directory**: Where converted files are saved (default: `$HOME/ffmpeg_converter`)
- **Codecs**: Video encoding options (copy, ProRes, H.264, etc.)
- **Audio normalization**: EBU R128 loudness processing optimized for genre (podcast, rock, edm, etc.)
- **Hardware acceleration**: GPU-based encoding for faster conversion
- **Custom ffmpeg 8.1**: Project-specific build with fdk_aac and soxr; must be co-located with binaries

---

## macOS Usage

macOS users have access to the native Cocoa GUI application, which is the recommended way to use ffmpeg_converter on this platform.

### Running the macOS GUI

1. **Open the app bundle:**
   ```bash
   open build/install/ffmpeg_converter_gui_macos.app
   ```

2. **Or locate it in Finder:**
   - Navigate to `build/install/`
   - Double-click `ffmpeg_converter_gui_macos.app`

### macOS GUI Workflow

#### Step 1: Add Files
- Click **"Add files..."** button
- Select one or more media files (`.mov`, `.mp4`, `.mkv`, etc.)
- Files appear in the file list

#### Step 2: Choose Codec
Select from available codecs in the dropdown:
- **copy** — passthrough without re-encoding (fastest)
- **prores** — ProRes codec for professional editing
- **prores_ks** — ProRes HQ codec for archive quality
- **prores_videotoolbox** — Apple VideoToolbox ProRes (hardware-accelerated)
- **hevc_videotoolbox** — Apple VideoToolbox H.265 (hardware-accelerated)
- **mux** — remux mode (replaces video track only)

#### Step 3: Audio Settings

**Audio Normalization:**
- **none** — no audio processing
- **peak** — normalize to peak level (single pass)
- **peak 2-pass** — loudness analysis + normalization (slower, more accurate)
- **loudness** — EBU R128 loudness normalization
- **loudness 2-pass** — loudness analysis + normalization (slower, more accurate)

**Audio Output Mode:**
- **pcm** — uncompressed audio
- **fdk_aac_320** — AAC audio via FDK AAC encoder (high quality; **requires ffmpeg 8.1 compiled with fdk_aac support**)
- **fdk_aac_320_ac3_640** — AAC (FDK) + Dolby Digital AC3 (**requires ffmpeg 8.1 with fdk_aac support**)

#### Step 4: Video Options (for ProRes codecs)

**Profile** (when using `prores` or `prores_ks`):
- **lt** — Light profile
- **standard** — Standard profile (recommended)
- **hq** — High Quality profile
- **4444** — 4:4:4 profile with alpha channel

**Deblock** (when using `prores` or `prores_ks`):
- **none** — no deblocking
- **weak** — light deblocking filter
- **strong** — stronger deblocking filter

#### Step 5: Genre (for Loudness Normalization)

When using loudness normalization, select the target genre:
- **edm** — electronic/dance music
- **rock** — rock music
- **hiphop** — hip-hop music
- **classical** — classical music
- **podcast** — speech/podcast audio

#### Step 6: Apple M4V Creator (optional)

To create Apple TV-compatible `.m4v` files:
1. Click **"M4V Edit"** button
2. Configure:
   - AAC quality (1–5, higher is better)
   - AC3 bitrate (384, 448, or 640 kbps)
   - Audio language (3-letter ISO code, e.g., `eng`, `rus`)
   - Include chapter markers (yes/no)
3. Select video and audio streams
4. Files are produced with `.m4v` extension

#### Step 7: Set Output Directory (optional)

- Click **"Output directory"** field
- Choose where to save converted files
- If not set, defaults to `~/ffmpeg_converter`

#### Step 8: Convert

Click **"Convert"** button. The progress window shows:
- File being processed
- Encoding percentage
- Frames per second (FPS)
- Estimated time remaining (ETA)
- Current status

### macOS CLI Usage

For command-line conversion on macOS:

```bash
# Basic usage
./build/src/cli/ffmpeg_converter input.mov

# ProRes with standard profile
./build/src/cli/ffmpeg_converter -c prores -p standard input.mov

# With audio normalization
./build/src/cli/ffmpeg_converter -c prores -a loudnorm -g rock input.mov

# Multiple files to specific output directory
./build/src/cli/ffmpeg_converter -o ~/Videos -c prores -p hq input1.mov input2.mov

# Mux mode (replace video track)
./build/src/cli/ffmpeg_converter -c mux --video-track replacement.hevc input.mkv
```

### macOS Requirements and Tips

- **ffmpeg/ffprobe 8.1**: Must be bundled in `build/install/ffmpeg_converter_gui_macos.app/Contents/Resources/bin/`
  - Verify with: `file build/install/ffmpeg_converter_gui_macos.app/Contents/Resources/bin/ffmpeg`
  - Must include fdk_aac and soxr support (verify: `ffmpeg -codecs | grep fdk_aac` and `ffmpeg -filters | grep soxr`)
- **No console window**: GUI applications run without terminal windows
- **Apple Silicon**: Intel binaries run transparently via Rosetta 2
- **Chapters support**: M4V creator imports chapter markers from source files using a
  metadata-transfer step (`ffmpeg -map_chapters`), which is stable for long chapter lists
  and filenames/titles containing Unicode characters

---

## Linux Usage

Linux users can choose between the C/CMake GUI (GTK4) or Pascal GUI (Lazarus), plus command-line tools.

### Running the Linux GUI

#### C Implementation (GTK4 GUI - Recommended)
```bash
# From repository root after building
./build/bin/ffmpeg_converter_gui

# Or run the AppImage (portable, single file)
./build/bin/FFMpeg-Converter-x86_64.AppImage
```

#### Pascal Implementation (Lazarus GUI)
```bash
./fpc/bin/ffmpeg_converter_gui
# AppImage variant
./fpc/bin/ffmpeg_converter_gui_fpc-x86_64.AppImage
```

### Linux GUI Workflow (C GTK4)

1. **Add Files** — click **Add files...** (Ctrl+O) or **drag and drop** files directly from your file manager onto the file list. Duplicate entries are silently ignored.
2. **Choose Codec** — hardware codecs (`h264_vaapi`, `hevc_vaapi`) are detected in the background; the list updates automatically when detection finishes.
3. **Audio Settings** — normalization mode and output format.
4. **Video Options** — profile and deblocking (for ProRes codecs).
5. **Set Output Directory** — optional; defaults to `~/ffmpeg_converter`.
6. **Start** — Ctrl+Return, or click **Start**.

**Keyboard shortcuts:**

| Shortcut | Action |
|----------|--------|
| Ctrl+O | Add files |
| Delete | Remove selected file |
| Ctrl+L | Clear file list |
| Ctrl+Return | Start conversion |
| Escape | Stop conversion |

**Paned layout:** drag the divider between the file queue and the log area to adjust their relative sizes.

**Special Actions:**
- **Remove / Clear** — remove selected or all files from queue.
- **Mux mode** — select `mux` in the codec dropdown; provide a replacement video track with **Add track...**.
- **Apple M4V** — click **Apple m4v...** to open the M4V creator options and produce an Apple-compatible M4V with AAC + AC3 dual audio.

### Linux CLI Usage

```bash
# Basic usage
./build/bin/ffmpeg_converter input.mp4

# ProRes with loudness normalization
./build/bin/ffmpeg_converter -c prores -a loudnorm2 -g podcast input.mp4

# Using hardware codec (auto-detected if available)
./build/bin/ffmpeg_converter -c h264_vaapi -a peak input.mp4

# Batch conversion with specific output directory
./build/bin/ffmpeg_converter -o ~/conversions -c prores file1.mov file2.mov file3.mov

# Mux mode: replace video, keep original audio
./build/bin/ffmpeg_converter -c mux --video-track new_video.hevc source.mkv

# Apple M4V creator (GUI-only on Linux, use command line on macOS)
# (CLI m4v support varies; check help output for your platform)
```

### Linux Hardware Codecs

Hardware codecs are **auto-detected** at runtime:

- **h264_vaapi** — H.264 encoding via VAAPI (if available)
- **hevc_vaapi** — H.265 encoding via VAAPI (if available)

These codecs only appear in the menu if your GPU and driver support them. To check:

```bash
ffmpeg -encoders | grep vaapi
```

### Linux AppImage

The AppImage is a single-file portable executable:

```bash
# Make executable (usually automatic)
chmod +x build/bin/FFMpeg-Converter-x86_64.AppImage

# Run
./build/bin/FFMpeg-Converter-x86_64.AppImage
```

Benefits:
- No installation required
- Works on different Linux distributions
- Includes ffmpeg, ffprobe, and other dependencies
- Can be copied and run on another Linux system

### Linux Requirements and Tips

- **ffmpeg/ffprobe 8.1**: Must be co-located in `build/bin/` alongside `ffmpeg_converter` binary
  - Verify version: `./build/bin/ffmpeg -version | head -1` (should show version 8.x)
  - Verify fdk_aac: `./build/bin/ffmpeg -codecs | grep fdk_aac`
  - Verify soxr: `./build/bin/ffmpeg -filters | grep soxr`
  - **Do not use system ffmpeg** — must use project-bundled version
- **GTK4 requirements**: Some distributions may need GTK4 development packages
- **AppImage performance**: Slightly slower startup due to FUSE mounting (negligible)
- **AppImage tools**: ffmpeg/ffprobe bundled inside AppImage; no additional setup needed

---

## Windows Usage

Windows users can choose between the C CLI (most complete) or Pascal CLI/GUI.

### Windows C CLI

The C implementation provides command-line conversion:

```bash
# From build directory
.\build-msvc\src\cli\Release\ffmpeg_converter.exe --help

# Or if copied to another location
ffmpeg_converter.exe input.mp4

# ProRes conversion
ffmpeg_converter.exe -c prores -p standard -a loudnorm input.mov

# Batch conversion
ffmpeg_converter.exe -o D:\output -c prores *.mov

# Mux mode
ffmpeg_converter.exe -c mux --video-track new_video.hevc input.mkv
```

### Windows Pascal GUI

The Pascal implementation provides both CLI and Lazarus GUI:

```bash
# GUI
fpc\gui\ffmpeg_converter_gui.exe

# CLI
fpc\cli\ffmpeg_converter_windows.exe input.mp4
```

#### Pascal GUI Features

The Lazarus GUI on Windows includes:

- **Vulkan GPU support**: Auto-detection of Vulkan devices
- **Vulkan device selector**: Choose which GPU to use for encoding
- **All codecs**: CPU ProRes, hardware encoders (NVIDIA/AMD/Intel/Vulkan)
- **Audio processing**: Same as other platforms
- **Threaded conversion**: Responsive UI during encoding
- **No console windows**: Clean operation without popup terminals

#### Vulkan ProRes on Windows

If your GPU supports Vulkan 1.1+, the Pascal GUI offers GPU-accelerated ProRes encoding:

1. Open GUI
2. Select codec: **prores_ks_vulkan**
3. In the action row, select desired device from **Vulkan device** dropdown
4. Set output options and convert

Available devices:
- `vulkan:0`, `vulkan:1`, etc. (automatically detected)
- Works with NVIDIA, AMD, Intel, and other Vulkan-capable GPUs

### Windows Codec Support

| Codec | Type | Availability |
|---|---|---|
| copy | Passthrough | Always |
| prores | Software CPU | Always |
| prores_ks | Software CPU | Always |
| h264_nvenc | NVIDIA GPU | If NVIDIA driver present |
| hevc_nvenc | NVIDIA GPU | If NVIDIA driver present |
| h264_amf | AMD GPU | If AMD GPU + runtime present |
| hevc_amf | AMD GPU | If AMD GPU + runtime present |
| h264_qsv | Intel GPU | If Intel Arc/UHD 770+ present |
| hevc_qsv | Intel GPU | If Intel Arc/UHD 770+ present |
| prores_ks_vulkan | Any GPU | If Vulkan 1.1+ GPU present |
| mux | MKV remux | If mkvmerge found |
| m4v | Apple M4V | If MP4Box found |

### Windows CLI Usage

```powershell
# Basic usage
ffmpeg_converter.exe input.mp4

# ProRes with audio normalization
ffmpeg_converter.exe -c prores -a loudnorm2 input.mov

# Set output directory
ffmpeg_converter.exe -o D:\conversions -c prores *.mov

# Overwrite existing output files
ffmpeg_converter.exe --overwrite -c prores input.mov

# Use hardware encoder (NVIDIA)
ffmpeg_converter.exe -c h264_nvenc input.mov

# Use Vulkan ProRes (Pascal CLI may vary)
# Check your implementation for Vulkan support in CLI
```

### Windows Requirements and Tips

- **ffmpeg/ffprobe 8.1**: Must be co-located in `build-msvc\src\cli\Release\` (or same directory as ffmpeg_converter.exe)
  - Copy ffmpeg.exe and ffprobe.exe from `src/platform/windows/bin/` to build output directory
  - Verify version: `ffmpeg.exe -version | findstr version` (should show version 8.x)
  - Verify fdk_aac: `ffmpeg.exe -codecs | findstr fdk_aac`
  - Verify soxr: `ffmpeg.exe -filters | findstr soxr`
  - **Do not use system ffmpeg** — must use project-bundled version
- **Output directory**: Supports Windows paths with backslashes
- **UNC paths**: Network paths like `\\server\share` are supported
- **MKVMERGE**: Install via Chocolatey: `choco install mkvtoolnix`
- **MP4BOX**: Install GPAC: `choco install gpac`
- **Long paths**: Enable long path support in Windows 10+ if needed

---

## Common Workflows

### Workflow 1: Archive ProRes for Editing

Store media in high-quality ProRes format for professional video editing:

```bash
# Single file
ffmpeg_converter -c prores -p hq input.mov

# Multiple files
ffmpeg_converter -c prores -p hq -o ~/Archive/ *.mov

# With deblocking for smoother quality
ffmpeg_converter -c prores -p hq -d weak input.mov
```

**When to use:**
- Archiving original media
- Preparing for professional editing
- Color grading workflows

---

### Workflow 2: Normalize Audio Levels

Ensure consistent audio loudness across files:

```bash
# Simple peak normalization (fast, 1 pass)
ffmpeg_converter -c copy -a peak input.mov

# EBU R128 loudness normalization (slower, 2 pass)
ffmpeg_converter -c copy -a loudnorm2 -g podcast input.mov
```

**When to use:**
- Video with inconsistent audio levels
- Podcast episodes that sound too loud/soft
- Creating content for streaming platforms

---

### Workflow 3: Create Apple TV-Compatible M4V

Produce `.m4v` files for Apple TV with multiple audio tracks:

**GUI:**
1. Add source files
2. Click **M4V Edit** or **M4V Create** button
3. Select video stream (usually 0)
4. Select audio stream (usually 0)
5. Set AAC quality (5 = best)
6. Set AC3 bitrate (640 kbps = loudest)
7. Configure language and chapters
8. Click **Convert**

**CLI (macOS only):**
```bash
# Using GUI M4V creator (recommended)
# CLI m4v support varies by platform
```

**Output:** `.m4v` file with:
- Passthrough video (h264, HEVC, or ProRes)
- AAC audio track (high quality)
- Dolby Digital AC3 audio track
- Optional chapter markers

---

### Workflow 4: Remux Video Tracks (Mux Mode)

Replace video track while keeping original audio:

```bash
# Replace video track in MKV
ffmpeg_converter -c mux --video-track replacement.hevc input.mkv

# Or via interactive menu:
# 1. Select 'mux' codec
# 2. Enter replacement video file when prompted
# 3. Optionally choose output directory
```

**When to use:**
- Re-encode video separately from audio
- Update video stream without re-encoding audio
- Combine separately processed video and audio

**Requirements:**
- `mkvmerge` installed
- Replacement video file is a raw stream (`.h264`, `.hevc`, `.h265`, `.264`) or container

---

### Workflow 5: Batch Convert Multiple Files

Convert many files in one command:

```bash
# Convert all .mov files in current directory
ffmpeg_converter -c prores *.mov

# Convert from subdirectories to organized output
ffmpeg_converter -o ~/converted -c prores -a loudnorm2 $(find . -name "*.mov")

# Windows: batch convert
ffmpeg_converter.exe -c prores D:\media\*.mov
```

**Tips:**
- Conversion happens sequentially (one file at a time)
- Progress bar shows current file status
- Use small files first to test settings
- Run during off-hours for long jobs

---

### Workflow 6: Stream-Copy for Fast Transcoding

Copy streams without re-encoding (fastest option):

```bash
# Copy video and audio, keep original codecs
ffmpeg_converter -c copy input.mov

# Fast output directory change without re-encoding
ffmpeg_converter -c copy -o ~/backup/ *.mkv
```

**When to use:**
- Container format conversion (MOV → MKV)
- Changing output directory quickly
- Fixing metadata without re-encoding

**Output container**: Depends on source; usually `.mov` or `.mkv`

---

## Codec Reference

### Video Codecs

#### copy
- **Description**: Passthrough without re-encoding
- **Speed**: Fastest (no processing)
- **Quality**: Original (lossless)
- **Use for**: Container conversion, quick backup
- **Available on**: All platforms

#### prores
- **Description**: Apple ProRes for professional editing
- **Speed**: Medium (software encode)
- **Quality**: Visually lossless
- **Profiles**: lt (light), standard, hq, 4444
- **Use for**: Archiving, editing workflows
- **Available on**: All platforms

#### prores_ks
- **Description**: ProRes HQ with extended color space
- **Speed**: Medium (software encode)
- **Quality**: Visually lossless, professional
- **Profiles**: lt, standard (recommended), hq, 4444
- **Use for**: High-end productions, archiving
- **Available on**: All platforms

#### prores_videotoolbox (macOS only)
- **Description**: Apple VideoToolbox hardware ProRes encoder
- **Speed**: Fast (hardware accelerated)
- **Quality**: Visually lossless
- **Use for**: Real-time encoding on macOS
- **Note**: Fallback to software on Intel Macs

#### hevc_videotoolbox (macOS only)
- **Description**: Apple VideoToolbox H.265 encoder
- **Speed**: Very fast (hardware accelerated)
- **Quality**: Lossy (high quality)
- **Bitrate**: Automatically calculated per file
- **Use for**: Web streaming, efficient archiving
- **Note**: Apple Silicon native, Rosetta 2 on Intel

#### h264_vaapi (Linux only, if available)
- **Description**: H.264 via VAAPI (Linux GPU acceleration)
- **Speed**: Very fast (GPU accelerated)
- **Quality**: Lossy
- **Use for**: Quick encoding on Linux with compatible GPU
- **Note**: Only available if driver supports VAAPI

#### hevc_vaapi (Linux only, if available)
- **Description**: H.265 via VAAPI (Linux GPU acceleration)
- **Speed**: Very fast (GPU accelerated)
- **Quality**: Lossy
- **Use for**: Quick H.265 encoding on Linux
- **Note**: Only available if driver supports VAAPI

#### h264_nvenc (Windows only, if available)
- **Description**: H.264 via NVIDIA encoder
- **Speed**: Very fast (GPU accelerated)
- **Quality**: Lossy (high quality)
- **Use for**: Quick H.264 encoding on NVIDIA GPU
- **Requires**: NVIDIA GPU + driver

#### hevc_nvenc (Windows only, if available)
- **Description**: H.265 via NVIDIA encoder
- **Speed**: Very fast (GPU accelerated)
- **Quality**: Lossy
- **Use for**: Efficient H.265 encoding on NVIDIA GPU
- **Requires**: NVIDIA GPU + driver

#### h264_amf (Windows only, if available)
- **Description**: H.264 via AMD AMF
- **Speed**: Very fast (GPU accelerated)
- **Quality**: Lossy
- **Use for**: AMD GPU H.264 encoding
- **Requires**: AMD GPU + AMF runtime

#### hevc_amf (Windows only, if available)
- **Description**: H.265 via AMD AMF
- **Speed**: Very fast (GPU accelerated)
- **Quality**: Lossy
- **Use for**: AMD GPU H.265 encoding
- **Requires**: AMD GPU + AMF runtime

#### h264_qsv (Windows only, if available)
- **Description**: H.264 via Intel Quick Sync
- **Speed**: Very fast (GPU accelerated)
- **Quality**: Lossy
- **Use for**: Intel Arc/UHD 770+ H.264 encoding
- **Requires**: Intel Arc GPU or 12th-gen+ CPU with UHD

#### hevc_qsv (Windows only, if available)
- **Description**: H.265 via Intel Quick Sync
- **Speed**: Very fast (GPU accelerated)
- **Quality**: Lossy
- **Use for**: Intel Arc/UHD H.265 encoding
- **Requires**: Intel Arc GPU or 12th-gen+ CPU with UHD

#### prores_ks_vulkan (Windows, any GPU with Vulkan 1.1+)
- **Description**: GPU-accelerated ProRes via Vulkan
- **Speed**: Fast (GPU accelerated)
- **Quality**: Visually lossless
- **Profile**: HQ (fixed)
- **Use for**: Cross-platform GPU ProRes encoding
- **Devices**: Auto-detected; select in GUI

### mux
- **Description**: Remux mode (replace video track only)
- **Input**: One source file + replacement video stream
- **Output**: `.mkv` via mkvmerge
- **Audio**: Copied from source (no re-encoding)
- **Use for**: Combining separately processed streams
- **Requires**: mkvmerge installed

### m4v
- **Description**: Apple M4V creator with dual audio (FDK AAC + AC3)
- **Video**: Stream copy (h264, HEVC, ProRes)
- **Audio**: AAC via FDK encoder (CBR 320k) + AC3 (Dolby Digital)
- **Output**: `.m4v` compatible with Apple TV
- **Use for**: Creating Apple TV-compatible files with high-quality audio
- **Requires**: MP4Box installed + ffmpeg 8.1 compiled with fdk_aac support
- **Note**: FDK AAC encoding requires project-bundled ffmpeg 8.1; system ffmpeg will fail

---

## Troubleshooting

### Issue: "ffmpeg not found" or "ffprobe not found"

**CRITICAL:** ffmpeg_converter requires **ffmpeg and ffprobe version 8.1 specifically compiled with fdk_aac and soxr support**. These tools MUST be bundled with project binaries in the same directory. System ffmpeg will NOT work.

**Cause**: ffmpeg/ffprobe are not in the project binary directory, or wrong version/missing codecs.

**Solutions**:

**Linux:**
```bash
# 1. Verify ffmpeg 8.1 with fdk_aac/soxr is in build/bin/
ls -la build/bin/ffmpeg build/bin/ffprobe

# 2. Verify version and codecs
./build/bin/ffmpeg -version | head -1
./build/bin/ffmpeg -codecs | grep fdk_aac
./build/bin/ffmpeg -filters | grep soxr

# 3. If missing, copy from src/platform/linux/bin/ or rebuild
cp src/platform/linux/bin/ffmpeg build/bin/
cp src/platform/linux/bin/ffprobe build/bin/

# 4. Run conversion (DO NOT use system ffmpeg)
./build/bin/ffmpeg_converter input.mov
```

**macOS:**
```bash
# 1. Verify ffmpeg 8.1 is bundled in app
ls build/install/ffmpeg_converter_gui_macos.app/Contents/Resources/bin/

# 2. Verify version and codecs
build/install/ffmpeg_converter_gui_macos.app/Contents/Resources/bin/ffmpeg -version | head -1
build/install/ffmpeg_converter_gui_macos.app/Contents/Resources/bin/ffmpeg -codecs | grep fdk_aac

# 3. If missing, rebuild app
cmake --build build --target macos_gui_native

# 4. Run app (do NOT use system ffmpeg)
open build/install/ffmpeg_converter_gui_macos.app
```

**Windows:**
```powershell
# 1. Verify ffmpeg 8.1 with fdk_aac/soxr is in build-msvc\src\cli\Release
dir build-msvc\src\cli\Release\ffmpeg.exe

# 2. Verify version and codecs
.\build-msvc\src\cli\Release\ffmpeg.exe -version | Select-Object -First 1
.\build-msvc\src\cli\Release\ffmpeg.exe -codecs | Select-String "fdk_aac"
.\build-msvc\src\cli\Release\ffmpeg.exe -filters | Select-String "soxr"

# 3. If missing, copy from src/platform/windows/bin/
copy src\platform\windows\bin\ffmpeg.exe build-msvc\src\cli\Release\
copy src\platform\windows\bin\ffprobe.exe build-msvc\src\cli\Release\

# 4. Copy all DLL dependencies as well
copy src\platform\windows\bin\*.dll build-msvc\src\cli\Release\

# 5. Run conversion (do NOT use system ffmpeg)
.\build-msvc\src\cli\Release\ffmpeg_converter.exe input.mov
```

**Verify bundled tools:**
```bash
# After ensuring ffmpeg/ffprobe are in correct directory
ffmpeg_converter -h  # Should work if tools are found
```

**If still failing:**
- Check that ffmpeg version is 8.x (not older)
- Verify fdk_aac codec is available (critical for audio workflows)
- Verify soxr filter is available (critical for audio normalization)
- Do NOT use system ffmpeg—only use project-bundled version

---

### Issue: "mkvmerge not found" when using mux mode

**Cause**: mkvmerge is required for mux workflow but not installed.

**Solutions**:

**Linux:**
```bash
sudo apt install mkvtoolnix  # Debian/Ubuntu
sudo dnf install mkvtoolnix  # Fedora
```

**macOS:**
```bash
brew install mkvtoolnix
# or
sudo port install mkvtoolnix
```

**Windows:**
```powershell
# Install via Chocolatey
choco install mkvtoolnix

# Or place mkvmerge.exe next to ffmpeg_converter.exe
```

**Verify:**
```bash
which mkvmerge  # Linux/macOS
where mkvmerge  # Windows
```

---

### Issue: "MP4Box not found" when creating M4V files

**Cause**: MP4Box (GPAC) is required for M4V workflow but not installed.

**Solutions**:

**Linux:**
```bash
sudo apt install gpac  # Debian/Ubuntu
sudo dnf install gpac  # Fedora
```

**macOS:**
```bash
brew install gpac
# or
sudo port install gpac
```

**Windows:**
```powershell
# Install via Chocolatey
choco install gpac

# Or download from https://gpac.io/
```

---

### Issue: "Hardware codec not available" on Linux

**Cause**: GPU and/or driver don't support the requested codec.

**Solutions**:

1. **Check available encoders:**
   ```bash
   ffmpeg -encoders | grep vaapi
   ```

2. **Check VAAPI availability:**
   ```bash
   vainfo  # Install libva-utils
   ```

3. **Use software encoder instead:**
   ```bash
   ffmpeg_converter -c prores -p standard input.mov
   ```

4. **Update GPU driver** for newer codec support

---

### Issue: Conversion is very slow

**Cause**: Using software encoding or large file.

**Solutions**:

1. **Use hardware encoder (if available):**
   ```bash
   # Windows: NVIDIA
   ffmpeg_converter -c h264_nvenc input.mov
   
   # macOS
   ffmpeg_converter -c hevc_videotoolbox input.mov
   ```

2. **Use stream copy if possible:**
   ```bash
   ffmpeg_converter -c copy input.mov
   ```

3. **Reduce output quality** (if using lossy codec):
   ```bash
   # For HEVC VideoToolbox on macOS, bitrate is auto-calculated
   # Conversion happens in real-time per frame
   ```

4. **Check system resources:**
   - Free disk space
   - CPU/GPU load
   - RAM availability

---

### Issue: Audio levels too loud/quiet after conversion

**Cause**: Audio normalization not applied, settings incorrect, or soxr filter unavailable.

**Solutions**:

1. **Verify soxr filter is available:**
   ```bash
   # Linux/macOS
   ./build/bin/ffmpeg -filters | grep soxr
   
   # Windows
   .\build-msvc\src\cli\Release\ffmpeg.exe -filters | findstr soxr
   ```
   If soxr is not found, rebuild ffmpeg 8.1 with soxr support.

2. **Apply loudness normalization (requires soxr):**
   ```bash
   ffmpeg_converter -a loudnorm2 -g podcast input.mov
   ```

3. **Adjust genre setting** based on content:
   - `podcast` for speech (most consistent)
   - `edm` for electronic music
   - `rock` for rock music
   - `hiphop` for hip-hop
   - `classical` for classical music

4. **Use peak normalization** (simpler, faster, no soxr needed):
   ```bash
   ffmpeg_converter -a peak input.mov
   ```

5. **Verify ffmpeg 8.1 with fdk_aac for AAC output:**
   ```bash
   ffmpeg_converter -a loudnorm2 -g podcast input.mov  # will use fdk_aac
   ```
   If audio output fails, verify fdk_aac is available.

6. **Verify source audio** is not already clipped

---

### Issue: Output file won't play on Apple TV

**Cause**: Container format or codec incompatibility.

**Solutions**:

1. **Use M4V creator workflow:**
   ```bash
   # GUI: Click M4V button
   # CLI: -c m4v
   ```

2. **Verify video codec** is compatible:
   - h264
   - HEVC (H.265)
   - ProRes

3. **Check audio** is AAC or AC3:
   ```bash
   # M4V workflow includes AAC + AC3
   ```

4. **Embed chapters** if source has them:
   - M4V creator includes chapter import

---

### Issue: GUI window won't open or crashes

**Cause**: Missing dependencies or permission issues.

**Solutions**:

**Linux:**
```bash
# Install GTK4 (for C GUI)
sudo apt install libgtk-4-0  # Debian/Ubuntu
sudo dnf install gtk4  # Fedora

# Run with verbose output
GTK_DEBUG=all ./build/bin/ffmpeg_converter_gui 2>&1 | head -50
```

**macOS:**
```bash
# Check if app is quarantined
xattr build/install/ffmpeg_converter_gui_macos.app

# Remove quarantine if needed
xattr -d com.apple.quarantine build/install/ffmpeg_converter_gui_macos.app

# Open with debugging
open -a build/install/ffmpeg_converter_gui_macos.app --verbose
```

**Windows:**
```powershell
# Run CLI instead (less dependency-heavy)
fpc\cli\ffmpeg_converter_windows.exe input.mov

# Or reinstall Lazarus runtime (for Pascal GUI)
```

---

### Issue: Permission denied when writing output

**Cause**: Output directory is not writable.

**Solutions**:

1. **Check directory permissions:**
   ```bash
   ls -ld ~/ffmpeg_converter  # Linux/macOS
   ```

2. **Create output directory:**
   ```bash
   mkdir -p ~/ffmpeg_converter
   chmod 755 ~/ffmpeg_converter
   ```

3. **Use different output directory:**
   ```bash
   ffmpeg_converter -o /tmp input.mov
   ```

4. **On Windows, check disk permissions:**
   - Right-click folder → Properties → Security
   - Ensure your user has Write permission

---

## Advanced Usage

### Using Environment Variables

**Note:** ffmpeg_converter prefers bundled ffmpeg/ffprobe in project binary directory. Environment variables are fallback only and should only be used if custom-compiled ffmpeg 8.1 with fdk_aac/soxr is needed:

```bash
# Linux/macOS
export FFMPEG_BIN=/custom/ffmpeg-8.1/bin/ffmpeg  # Must be version 8.1 with fdk_aac/soxr
export FFPROBE_BIN=/custom/ffmpeg-8.1/bin/ffprobe
export MKVMERGE_BIN=/custom/path/mkvmerge
export MP4BOX_BIN=/custom/path/MP4Box

# Windows PowerShell
$env:FFMPEG_BIN = "C:\ffmpeg-8.1\ffmpeg.exe"  # Must be version 8.1 with fdk_aac/soxr
$env:FFPROBE_BIN = "C:\ffmpeg-8.1\ffprobe.exe"
```

**Verify custom ffmpeg meets requirements:**
```bash
# Verify version is 8.x
/custom/ffmpeg/ffmpeg -version | head -1

# Verify fdk_aac is available
/custom/ffmpeg/ffmpeg -codecs | grep fdk_aac

# Verify soxr is available
/custom/ffmpeg/ffmpeg -filters | grep soxr
```

### Custom Genre for Loudness Normalization

Specify genre to optimize loudness normalization:

```bash
ffmpeg_converter -a loudnorm2 -g rock music.mov     # Rock music
ffmpeg_converter -a loudnorm2 -g edm beat.mov       # Electronic music
ffmpeg_converter -a loudnorm2 -g podcast talk.mov   # Podcasts/speech
```

### Interactive Menu Mode (CLI)

Without arguments, enter interactive mode:

```bash
ffmpeg_converter
# Then follow the 9-step menu to configure conversion
```

---

## Contact & Support

For issues, questions, or feature requests:

- **Repository**: Check the main README.md
- **Documentation**: See `docs/` folder for detailed guides
- **Platform-specific**: See `docs/install-linux.md`, `docs/install-macos.md`, `docs/install-windows.md`

---

## License

ffmpeg_converter is released under the MIT License. See LICENSE file for details.
