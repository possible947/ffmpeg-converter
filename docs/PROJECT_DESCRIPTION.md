# ffmpeg_converter — Developer Description (Version 2.5)

This document describes the current, factual state of the repository as of v2.5.
It is intentionally concise and aligned with the code and build files.

## 1. Project Scope

ffmpeg_converter is a cross-platform media conversion project with two
independent implementations:

- **C/CMake** in `src/` (primary on all platforms)
- **Free Pascal** in `fpc/` (Linux and Windows only; macOS discontinued in v2.4)

Both paths provide conversion workflows around external `ffmpeg`/`ffprobe`.

## 2. Implementations (v2.5)

### 2.1 C/CMake (`src/`)

**Platform coverage:**
- **macOS**: CLI + native Cocoa GUI (sole macOS implementation)
- **Linux**: CLI + GTK4 GUI
- **Windows**: CLI (most complete version, MSVC build only)

**Key modules:**
- Core engine: `src/converter/converter.c`, `src/converter/converter.h`
- CLI entry points: `src/cli/linux/main.c`, `src/cli/macos/main.c`, `src/cli/windows/main.c`
- Linux GUI (GTK4): `src/gui/`
- macOS native GUI (Cocoa/AppKit): `src/gui_macos_native/`
- Mux module: `src/mux/`
- Apple M4V module: `src/m4v/`

**Codecs:**
- Cross-platform: `copy`, `prores`, `prores_ks`
- Linux (VAAPI, runtime-detected): `h264_vaapi`, `hevc_vaapi`
- macOS (VideoToolbox): `prores_videotoolbox`, `hevc_videotoolbox`
- Windows (GPU runtime-detected): NVIDIA NVENC, AMD AMF, Intel QSV, Vulkan ProRes
- All platforms: AV1 input decoding (auto-selected decoder)

**Audio normalization:**
- `none`, `peak`, `peak_2pass`, `loudness`, `loudness_2pass`
- 2-pass uses `-filter_threads N` for parallel processing

**Special workflows:**
- Mux mode: one source file + replacement video track → `.mkv` via `mkvmerge`
- Apple M4V creator: multi-step pipeline (video copy → AAC → AC3 → MP4Box mux → optional chapters)

### 2.2 Free Pascal (`fpc/`) — Linux and Windows Only

**Platform coverage:**
- **Linux**: CLI + Lazarus/LCL GUI (feature-matched with C)
- **Windows**: CLI + Lazarus/LCL GUI with Vulkan GPU support (feature-matched with C CLI)
- **macOS**: Discontinued in v2.4

**Key modules:**
- Engine: `fpc/converter/converter_core.pas`
- CLI: `fpc/cli/ffmpeg_converter.lpr` (Linux), `fpc/cli/ffmpeg_converter_windows.lpr` (Windows)
- GUI: `fpc/gui/form.pas` (Lazarus/LCL)
- Apple M4V: `fpc/converter/apple_m4v_creator.pas`
- JSON parsing: `fpc/json/loudnorm_json.pas`
- Tests: `fpc/test/test_*.pas`

**Codec support:**
- Feature-parity with C on Linux and Windows
- Windows: runtime probing for Vulkan device selection in GUI

**C ABI export (for library usage):**
- `fpc/converter/converter_pas.lpr` exports C-compatible shared library
- 7 exported functions: `converter_create`, `converter_destroy`, `converter_set_callbacks`,
  `converter_set_options`, `converter_process_files`, `converter_stop`, `converter_error_string`

## 3. Apple M4V Workflow

Implemented in both C and Pascal (where available):

- **C macOS**: `src/gui_macos_native/apple_m4v_creator.m` and bridge in `converter_bridge.m`
- **C Linux**: GTK GUI action; shared backend in `src/m4v/`
- **Pascal (Linux/Windows)**: `fpc/converter/apple_m4v_creator.pas`

**Pipeline (all platforms):**
1. Extract video track to temporary `.mp4` (stream copy)
2. Encode AAC audio (`libfdk_aac -vbr 5`)
3. Encode AC3 audio (configurable bitrate: 384/448/640 kbps)
4. Mux tracks into `.m4v` via `MP4Box`
5. Embed chapter markers (optional) by transferring chapter metadata from source
   with `ffmpeg -map_chapters 1 -c copy`

**Supported video codecs (preflight check):**
- `h264`, `hevc`, `prores` (others rejected with clear error)

## 4. Build Targets (v2.5)

### 4.1 C/CMake targets

**Linux:**
- `linux_cli` — CLI binary
- `linux_gui` — GTK4 GUI binary
- `package_appimage` — AppImage (optional, requires `ENABLE_APPIMAGE=ON`)

**macOS (C only):**
- `macos_cli` — CLI binary
- `macos_gui_native` — native Cocoa GUI
- `MACOS_BUNDLE_INFO_PLIST` — Info.plist for `.app` bundle

**Windows (MSVC):**
- `windows_cli` — CLI binary (most complete)
- Use `./scripts/windows_build.ps1` or manual CMake

**CMake feature switches:**
- `ENABLE_LINUX_GUI` (default ON on Linux)
- `ENABLE_MACOS_NATIVE_GUI` (default ON on macOS)

### 4.2 Pascal/Make targets

In `fpc/build/Makefile`:
- `cli` — CLI binary
- `lib` — shared library (C ABI export)
- `gui-app` — Lazarus GUI app bundle
- `tests` — all unit tests

**Platform-specific:**
- Linux: produces `fpc/cli/ffmpeg_converter`
- Windows: produces `fpc/cli/ffmpeg_converter_windows.exe`
- Linux GUI: produces `fpc/gui/form.app`

## 5. Runtime Dependencies

### C Path
- `ffmpeg`, `ffprobe` (bundled on all platforms, or discovered via env/PATH)
- `jansson` (system library for loudnorm JSON parsing)
- `mkvmerge` (optional, for mux mode)
- `MP4Box` (optional, for Apple M4V creator)
- **Linux GUI**: GTK4
- **macOS GUI**: AppKit (native framework, no GTK)

### Pascal Path
- `ffmpeg`, `ffprobe` (bundled or discovered)
- `mkvmerge` (optional, for mux mode)
- `MP4Box` (optional, for Apple M4V creator)
- **For builds**: FPC compiler + Lazarus IDE (for GUI)

### Tool Discovery Priority (all platforms, C and Pascal)
1. Executable-adjacent directory (next to binary)
2. Environment variables: `FFMPEG_BIN`, `FFPROBE_BIN`, `MKVMERGE_BIN`, `MP4BOX_BIN`
3. System `PATH`

**macOS specifics (C only):**
- Checks MacPorts paths first: `/opt/local/bin/ffmpeg8` → `/opt/local/bin/ffmpeg`
- Falls back to bundled or system PATH

## 6. Known Boundaries

- Windows C GUI is not implemented.
- C CLI supports `--dry-run` and `--version`.

## 7. Canonical References

- User-facing overview: `README.md`
- C changelog: `CHANGELOG.md`
- Pascal changelog: `fpc/CHANGELOG.md`
- Install guides: `docs/install-linux.md`, `docs/install-macos.md`,
  `docs/install-windows.md`
- Apple M4V design/status: `docs/macos-native-apple-m4v-design.md`
