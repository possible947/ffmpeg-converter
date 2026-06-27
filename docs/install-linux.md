# Linux Install and Build

This document covers Linux install/build for both project paths:
- C/CMake (`src/`)
- Free Pascal (`fpc/`)

## 1. C/CMake Path

### 1.1 Install dependencies
Debian/Ubuntu:
```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config ffmpeg libjansson-dev libgtk-4-dev
```

Fedora:
```bash
sudo dnf install -y gcc gcc-c++ make cmake pkgconf-pkg-config ffmpeg jansson-devel gtk4-devel
```

> **AV1 input decoding note:** The `ffmpeg` package from the default Debian/Ubuntu APT
> repositories may not include `libdav1d` support. To decode AV1 source files reliably,
> use an ffmpeg build that includes `--enable-libdav1d`. Options:
> - Ubuntu 22.04+: `sudo apt install ffmpeg` usually includes libdav1d.
> - Debian stable: consider [deb-multimedia.org](https://deb-multimedia.org/) or a
>   custom build.
> - Fedora: `ffmpeg` from RPM Fusion includes libdav1d.
> - Or place a custom-built `ffmpeg`/`ffprobe` in `src/platform/linux/bin/` — the
>   converter prefers the bundled binary over the system one.
> If libdav1d is unavailable at runtime, the converter falls back to the native `av1`
> decoder with `-hwaccel none` (may fail on systems with NVIDIA GPUs that lack AV1
> NVDEC support).

### 1.2 Build targets
From repository root:
```bash
mkdir -p build
cd build
cmake ..
cmake --build . --target linux_cli
cmake --build . --target linux_gui
```

Artifacts in the flat Linux layout:
- `build/bin/ffmpeg_converter`
- `build/bin/ffmpeg_converter_gui`
- `build/bin/ffmpeg`
- `build/bin/ffprobe`
- `build/bin/mkvmerge` when found in common Linux system locations
- `build/bin/MP4Box` when found in common Linux system locations

### 1.3 AppImage packaging (optional)
AppImage produces a single-file portable executable that bundles the GUI, ffmpeg/ffprobe, and required libraries.

Requires `appimagetool.AppImage` (or `appimagetool`) in PATH. Download from https://github.com/AppImage/AppImageKit/releases.

```bash
# From the repository root (build directory must already exist)
cmake -S . -B build -DENABLE_APPIMAGE=ON
cmake --build build --target linux_gui
cmake --build build --target package_appimage
```

The AppImage is created at `build/bin/ffmpeg_converter_gui-x86_64.AppImage` (≈71 MB).

**Important:** `ffmpeg` and `ffprobe` must be present in `src/platform/linux/bin/` before packaging — the script enforces this and will exit with an error otherwise.

Alternatively, run the packaging script directly:
```bash
bash src/gui/package_appimage.sh "$(pwd)/build" "$(pwd)/build/bin"
```

The script:
- Requires project-built `ffmpeg`/`ffprobe` in `src/platform/linux/bin/` (mandatory)
- Copies optional tools (`mkvmerge`, `MP4Box`) from project dir, `build/bin`, or system PATH
- Resolves shared library dependencies via `ldd`, excluding system libraries (`/lib`, `/usr/lib` paths)
- Generates `AppRun` wrapper that:
  - Sets `PATH` and `LD_LIBRARY_PATH` to include bundled libraries
  - Exports tool-specific environment variables (`FFMPEG_BIN`, `FFPROBE_BIN`, `MKVMERGE_BIN`, `MP4BOX_BIN`)
    for each bundled tool (only if present in the AppImage)
  - Ensures bundled tools are discovered first by the converter application
- Creates a desktop entry with icon
- Invokes `appimagetool.AppImage` (or `appimagetool`) to produce the final `.AppImage`

## 2. Free Pascal Path (v2.5: Feature-complete, tested)

### 2.1 Install dependencies
```bash
sudo apt install -y fpc lazarus
```

### 2.2 Build targets
From repository root:
```bash
# CLI binary
make -C fpc/build cli
# → fpc/bin/ffmpeg_converter

# Shared library
make -C fpc/build lib
# → fpc/converter/libconverter_pas.so

# GUI binary
make -C fpc/build gui
# → fpc/bin/ffmpeg_converter_gui

# Unit tests
make -C fpc/build tests
```

Artifacts:
- `fpc/bin/ffmpeg_converter` — CLI binary (parity with C CLI)
- `fpc/converter/libconverter_pas.so` — shared library (C ABI export)
- `fpc/bin/ffmpeg_converter_gui` — GUI binary
- `fpc/test/test_*` — unit test executables

### 2.3 AppImage packaging (optional)
Packages the FPC GUI into a portable single-file AppImage.

Requires `appimagetool.AppImage` (or `appimagetool`) in PATH and project-built `ffmpeg`/`ffprobe` in `src/platform/linux/bin/`.

```bash
make -C fpc/build gui
make -C fpc/build appimage
```

The AppImage is created at `fpc/bin/ffmpeg_converter_gui_fpc-x86_64.AppImage`.

Alternatively, run the script directly:
```bash
bash fpc/build/package_appimage.sh
```

Bundling behaviour (same as C variant):
- `ffmpeg`/`ffprobe` — mandatory, taken from `src/platform/linux/bin/`
- `mkvmerge`, `MP4Box` — optional, checked in order: project dir (`src/platform/linux/bin/`), 
  `build/bin`, system PATH
- Shared libraries resolved via `ldd`, system paths excluded
- AppRun wrapper exports tool-specific environment variables for each bundled tool

## 3. Runtime Notes

### Both C and Pascal: Tool Discovery
- **Bundled tool priority**: executable-adjacent directory → `src/platform/linux/bin/` (dev) → env vars → `PATH`
- Environment variable overrides:
  - `FFMPEG` / `FFMPEG_BIN` → ffmpeg binary path
  - `FFPROBE` / `FFPROBE_BIN` → ffprobe binary path
  - `MKVMERGE_BIN` → mkvmerge path (for mux mode)
  - `MP4BOX_BIN` → MP4Box path (for Apple M4V workflow)

### AppImage-specific behavior
When running the application via AppImage (`.AppImage` file):
- The `AppRun` wrapper sets `PATH` and `LD_LIBRARY_PATH` to include bundled resources first
- Tool environment variables are automatically set for each bundled tool (e.g., `FFMPEG_BIN` is set 
  to the bundled ffmpeg path if it exists within the AppImage)
- This ensures the converter finds and uses bundled tools without requiring user configuration

### CLI Behavior (both C and Pascal)
- Inputs are positional arguments (`ffmpeg_converter [options] file1 file2 ...`).
- `-o/--output` sets output directory (not filename).
- Default output directory is `$HOME/ffmpeg_converter` when `-o` is omitted.
- `codec=mux` requires exactly one source file and `--video-track <file>` for replacement video.
- Final mux output is always `.mkv` via `mkvmerge`.

### GUI-only Features (C GTK on Linux)
- Apple M4V creator button (available alongside standard conversion and mux workflows).
- Audio output selector with modes: `pcm`, `fdk_aac_q5`, `fdk_aac_q5_ac3_640`.
- Runtime VAAPI codec probing: `h264_vaapi` and `hevc_vaapi` appear only on compatible hardware.

### Hardware Codecs (Runtime Detected)
- C and Pascal both probe Linux hardware support at startup.
- `h264_vaapi` and `hevc_vaapi` exposed only when the system has working VAAPI driver.
- AV1 input decoding is automatic: selects `av1_qsv` (Intel Arc), then `libdav1d` (software),
  then `av1` with `-hwaccel none`. Requires ffmpeg with `--enable-libdav1d`.

### Packaging and Deployment
- To run the toolset from another directory, copy or symlink `build/bin/*` to `~/.local/bin`.
- `MP4Box` is a single binary; no shared-library bundle created. Ensure target system
  has compatible GPAC runtime libraries.
- **AppImage (C GUI):** `cmake -S . -B build -DENABLE_APPIMAGE=ON && cmake --build build --target package_appimage`
  — output: `build/bin/ffmpeg_converter_gui-x86_64.AppImage` (~71 MB).
  - Usage: `./ffmpeg_converter_gui-x86_64.AppImage` (may require `chmod +x` first)
  - Contains all bundled ffmpeg, ffprobe, and optional mkvmerge/MP4Box
  - Single-file deployment: copy to any Linux x86_64 system
- **AppImage (FPC GUI):** `make -C fpc/build appimage`
  — output: `fpc/bin/ffmpeg_converter_gui_fpc-x86_64.AppImage`.
  - Usage and deployment same as C variant
- Both AppImage builds require:
  - `appimagetool.AppImage` (or `appimagetool`) in PATH for building
  - Project-built `ffmpeg`/`ffprobe` in `src/platform/linux/bin/` before packaging
  - Linux x86_64 host for execution (not compatible with other architectures)

## 4. Validation and Testing

### C Path
```bash
cmake --build build --target linux_cli
./build/bin/ffmpeg_converter --help
```

### Pascal Path
```bash
# Run all unit tests
make -C fpc/build tests

# Run specific tests
./fpc/test/test_cmd_builder
./fpc/test/test_cli_mode_matrix
./fpc/test/test_unified_tool_resolver

# Full regression suite (requires test.mp4)
bash fpc/test/run_all_regression_and_capture.sh
```
