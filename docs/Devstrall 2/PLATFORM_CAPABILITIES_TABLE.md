# ffmpeg-converter Platform Capabilities Comparison

## Summary Table of Program's Capabilities by Platform

### Legend
- ✓ = Supported
- - = Not applicable/not supported
- * = Requires additional installation
- (runtime) = Detected at runtime based on available hardware/drivers

### Video Codec Support

| Feature | Linux (C) | macOS (C) | Windows (C) | Linux (FPC) | Windows (FPC) |
|---------|-----------|-----------|-------------|--------------|---------------|
| **copy** (stream copy) | ✓ | ✓ | ✓ | ✓ | ✓ |
| **prores** (CPU-based ProRes) | ✓ | ✓ | ✓ | ✓ | ✓ |
| **prores_ks** (Kostya's ProRes) | ✓ | ✓ | ✓ | ✓ | ✓ |
| **h264_vaapi** (VAAPI H.264 encode) | ✓ (runtime) | - | - | ✓ (runtime) | - |
| **hevc_vaapi** (VAAPI HEVC encode) | ✓ (runtime) | - | - | ✓ (runtime) | - |
| **prores_videotoolbox** (Apple VideoToolbox ProRes) | - | ✓ | - | - | - |
| **hevc_videotoolbox** (Apple VideoToolbox HEVC) | - | ✓ | - | - | - |
| **h264_nvenc** (NVIDIA NVENC H.264) | - | - | ✓ (runtime) | - | ✓ (runtime) |
| **hevc_nvenc** (NVIDIA NVENC HEVC) | - | - | ✓ (runtime) | - | ✓ (runtime) |
| **h264_amf** (AMD AMF H.264) | - | - | ✓ (runtime) | - | ✓ (runtime) |
| **hevc_amf** (AMD AMF HEVC) | - | - | ✓ (runtime) | - | ✓ (runtime) |
| **h264_qsv** (Intel QSV H.264) | - | - | ✓ (runtime) | - | ✓ (runtime) |
| **hevc_qsv** (Intel QSV HEVC) | - | - | ✓ (runtime) | - | ✓ (runtime) |
| **prores_ks_vulkan** (Vulkan-accelerated ProRes) | - | - | ✓ (runtime) | - | ✓ (runtime) |

### Audio Features

| Feature | Linux (C) | macOS (C) | Windows (C) | Linux (FPC) | Windows (FPC) |
|---------|-----------|-----------|-------------|--------------|---------------|
| **PCM audio output** | ✓ | ✓ | ✓ | ✓ | ✓ |
| **FDK AAC 320k CBR** | ✓ | ✓ | ✓ | ✓ | ✓ |
| **FDK AAC + AC3 dual audio** | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Peak normalization (single pass)** | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Peak normalization (2-pass)** | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Loudness normalization (EBU R128, single pass)** | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Loudness normalization (EBU R128, 2-pass)** | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Genre-specific loudness targets** | ✓ | ✓ | ✓ | ✓ | ✓ |

### Special Features

| Feature | Linux (C) | macOS (C) | Windows (C) | Linux (FPC) | Windows (FPC) |
|---------|-----------|-----------|-------------|--------------|---------------|
| **MKV mux mode** (replace video track) | ✓* | ✓* | ✓* | ✓* | ✓* |
| **Apple M4V creator** (multi-track H.265 + AAC + AC3) | ✓ (GUI only) | ✓ | - | ✓ (CLI/GUI) | - |
| **AV1 input decoding** | ✓ (runtime) | - | ✓ (runtime) | ✓ (runtime) | ✓ (runtime) |
| **Hardware acceleration detection** | ✓ | ✓ | ✓ | ✓ | ✓ |
| **VAAPI device probing** | ✓ | - | - | ✓ | - |
| **VideoToolbox support** | - | ✓ | - | - | - |
| **Vulkan GPU support** | - | - | ✓ | - | ✓ |
| **Multi-threading for audio filters** | ✓ | ✓ | ✓ | ✓ | ✓ |

### User Interface Options

| Feature | Linux (C) | macOS (C) | Windows (C) | Linux (FPC) | Windows (FPC) |
|---------|-----------|-----------|-------------|--------------|---------------|
| **Command-line interface** | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Interactive menu mode** | ✓ | ✓ | ✓ | ✓ | ✓ |
| **GTK4 graphical interface** | ✓ | - | - | ✓ | - |
| **Cocoa native graphical interface** | - | ✓ | - | - | - |
| **Lazarus LCL graphical interface** | - | - | - | ✓ | ✓ |
| **Drag-and-drop file support** | ✓ (GUI) | ✓ (GUI) | - | ✓ (GUI) | ✓ (GUI) |
| **AppImage packaging** | ✓ | - | - | ✓ | - |

### Platform-Specific Notes

#### Linux (C Implementation)
- GTK4 GUI with native look and feel
- VAAPI hardware acceleration for H.264/HEVC
- AppImage packaging support
- Runtime codec detection via VAAPI probing
- MKV mux mode requires mkvmerge from MKVToolNix

#### macOS (C Implementation)
- Native Cocoa GUI with drag-and-drop
- VideoToolbox hardware acceleration for ProRes and HEVC
- Self-contained .app bundle with bundled ffmpeg/ffprobe/MP4Box
- Apple M4V creator with multi-track output
- MKV mux mode requires mkvmerge

#### Windows (C Implementation)
- MSVC build system with PowerShell/CMD scripts
- Comprehensive GPU support: NVENC, AMF, QSV, Vulkan
- Bundled ffmpeg/ffprobe binaries
- UTF-8 console support for international paths
- MKV mux mode requires mkvmerge (Chocolatey, MSYS2, or manual install)

#### Linux (Free Pascal Implementation)
- Lazarus LCL-based GUI
- Same VAAPI runtime probing as C version
- AppImage packaging support
- Complete feature parity with C CLI
- Shared library export for embedding

#### Windows (Free Pascal Implementation)
- Vulkan GPU support for ProRes encoding
- Native Windows GUI with no console popups
- Feature-matched with C CLI implementation
- Comprehensive codec detection via runtime probing
- Shared library export for embedding

### Requirements Summary

| Requirement | Linux | macOS | Windows |
|-------------|-------|-------|---------|
| **ffmpeg + ffprobe** | ✓* | ✓ (bundled) | ✓ (bundled) |
| **jansson library** | ✓ | ✓ | - |
| **libgtk-4-dev** | ✓ (GUI only) | - | - |
| **MP4Box (GPAC)** | ✓* (M4V mode) | ✓ (bundled) | - |
| **mkvmerge** | ✓* (mux mode) | ✓* | ✓* |
| **VAAPI drivers** | ✓* (hardware encode) | - | - |
| **Xcode command-line tools** | - | ✓ (GUI only) | - |
| **MSVC 2022** | - | - | ✓ (C implementation) |
| **Lazarus/FPC** | - | - | ✓ (Pascal implementation) |

*Can be installed via package manager or placed next to executable