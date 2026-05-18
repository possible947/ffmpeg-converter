# ffmpeg_converter 2.4

Cross-platform media conversion and mux tool with CLI and GUI for building and
running optimized `ffmpeg` workflows. Version 2.4 unifies feature sets across
platforms, stabilizes macOS (C-only), and completes Linux and Windows implementations
with full feature parity.

Two independent implementations share the same conversion logic and CLI behavior:

- **C/CMake** (`src/`) — primary engine; macOS native Cocoa GUI, Linux GTK4 GUI,
  Windows CLI (MSVC build).
- **Free Pascal** (`fpc/`) — complete port with CLI and GUI; available for Linux
  and Windows (macOS version discontinued).

## Version 2.4 Updates

### macOS (C only, no Pascal)
- **Native Cocoa GUI now the only GUI option** — Pascal macOS packaging removed.
- Full feature parity with Linux/Windows: mux mode, Apple M4V creator, audio
  normalization, codec selection.
- Self-contained `.app` bundle with bundled `ffmpeg`, `ffprobe`, `MP4Box`,
  and `mkvmerge`.
- Stable platform (no new functions added in v2.4; focus on reliability).

### Linux (C + Pascal, feature-matched)
- **Both C and Pascal versions complete and tested with identical functionality**.
- New build system: `make -C fpc/build cli`, `make -C fpc/build gui-app`,
  `make -C fpc/build tests`.
- Runtime tool discovery (ffmpeg, ffprobe, mkvmerge, MP4Box) now unified across
  implementations.
- VAAPI codec runtime probing in both implementations.
- AppImage packaging support (C and FPC).

### Windows (C CLI primary, C/Pascal GUI options)
- **C CLI is the most complete version** — full functionality, MSVC build, bundled
  binaries, new PowerShell/CMD build scripts.
- **Windows Pascal CLI and GUI** — complete and tested, feature-matched with C CLI,
  with native Vulkan ProRes encoder support in GUI.
- New build system: unified CMake integration with FPC targets.
- Codec support: CPU ProRes, GPU accelerators (NVIDIA/AMD/Intel/Vulkan), AV1
  input decoding, mux mode, Apple M4V creator.

---

## Features

- Video codecs (cross-platform): `copy`, `prores`, `prores_ks`.
- Linux runtime-probed video codecs: `h264_vaapi`, `hevc_vaapi`.
- Video codecs (macOS VideoToolbox): `prores_videotoolbox`, `hevc_videotoolbox`.
- **AV1 input decoding**: runtime-detected; uses `av1_qsv` (Intel QSV/Arc) when available,
  falls back to `libdav1d` (pure software). Requires ffmpeg built with `--enable-libdav1d`.
- Audio normalization: `none`, `peak`, `peak 2-pass`, `loudness`, `loudness 2-pass`.
- Audio output modes: PCM, FDK AAC q5, FDK AAC q5 + AC3 640.
- Linux MKV mux mode: one source file + external replacement video track, final output via `mkvmerge`.
- **Windows MKV mux mode**: same workflow available on Windows when `mkvmerge` is found on PATH or next to the executable (installed via MKVToolNix, Chocolatey, or MSYS2).
- **Audio filter multithreading**: 2-pass analysis uses `-filter_threads N` (N = CPU/2) for parallel audio processing.
- Encode progress: percent, FPS, ETA.
- CLI with argument parsing and interactive menu.
- **Linux GUI** — GTK4 (C implementation). Build produces `linux_gui` binary; optional AppImage packaging 
  available via `ENABLE_APPIMAGE=ON` and `package_appimage` target (produces single-file portable AppImage).
  Pascal GUI also supports AppImage packaging: `make -C fpc/build appimage`.
- **macOS GUI** — native Cocoa/AppKit, self-contained `.app` bundle with bundled
  `ffmpeg`, `ffprobe`, and `MP4Box` (C native implementation).
- Linux GTK Apple M4V creator: dedicated GUI-only workflow matching the macOS direct M4V path.
- Apple M4V creator: multi-step pipeline (video copy + AAC + AC3 + MP4Box mux
  + optional chapter import) in both Pascal GUI and C native macOS GUI.

---

## Requirements

### C/CMake path
- `cmake` ≥ 3.16, C compiler (clang/gcc on Linux/macOS; MSVC on Windows).
- `jansson` library (JSON parsing for loudnorm).
- `ffmpeg` + `ffprobe` (platform-specific bundling):
  - **Linux**: staged next to CLI/GUI in `build/bin` when available from `src/platform/linux/bin/`.
  - **macOS**: bundled inside native `.app` from `src/platform/macos/bin/`; CLI also
    checks MacPorts paths (`/opt/local/bin`, `/opt/homebrew/bin`).
  - **Windows (MSVC)**: required in `src/platform/windows/bin/` (`ffmpeg.exe`, `ffprobe.exe`,
    and all DLL dependencies); copied next to `ffmpeg_converter.exe` at build time.
- **AV1 input decoding** (Linux/Windows): requires ffmpeg compiled with `--enable-libdav1d`
  and `libdav1d-7.dll` present in `bin/` folder (already included in default bundled sets).
- `MP4Box` (GPAC) for Apple M4V packaging on macOS native GUI and Linux GTK M4V workflow.
- `mkvmerge` for mux mode on all platforms (Linux, macOS, Windows).
  - Windows: install via Chocolatey (`choco install mkvtoolnix`), MSYS2 (`pacman -S mingw-w64-x86_64-mkvtoolnix`),
    or place `mkvmerge.exe` next to `ffmpeg_converter.exe`.
  - Environment variables: `MKVMERGE` or `MKVMERGE_BIN` override binary path.
  - Mux mode silently disabled if mkvmerge not found.
- Linux GUI only: `libgtk-4-dev` (or distro equivalent).
- macOS GUI only: Xcode command-line tools (includes clang, libtool).
- Optional AppImage packaging (Linux): `appimagetool` (https://github.com/AppImage/AppImageKit).

### Free Pascal path (Linux and Windows)
- **Linux & Windows**: Lazarus IDE + FPC (for GUI), or plain `fpc` compiler (for CLI/library).
- **macOS**: Pascal support discontinued in v2.4.
- `ffmpeg` + `ffprobe` for Linux bundling in `src/platform/linux/bin/` (not required for CLI,
  but used for GUI packaging).
- `mkvmerge` for Pascal `mux` workflow (`codec=mux`) and GUI post-mux stage.
- **Windows GPU support**: Vulkan device probing in GUI (any GPU with Vulkan 1.1+).

---

## Quick Build

### C/CMake — Linux

```bash
mkdir build && cd build
cmake ..
cmake --build . --target linux_cli
cmake --build . --target linux_gui
```

**AppImage package (optional):**
```bash
cmake -S . -B build -DENABLE_APPIMAGE=ON
cmake --build build --target linux_gui
cmake --build build --target package_appimage
# Output: build/bin/ffmpeg_converter_gui-x86_64.AppImage (~71 MB)
```
Requires `appimagetool` in PATH. The script `src/gui/package_appimage.sh` can
also be invoked directly with custom output directory.

### C/CMake — macOS (native Cocoa GUI)

```bash
mkdir build && cd build
cmake ..
cmake --build . --target macos_cli
cmake --build . --target macos_gui_native
cmake --install .   # produces build/install/ffmpeg_converter_gui_macos.app
```

### C/CMake — Windows (MSVC)

Prepare bundled binaries before build:

```text
src/platform/windows/bin/
  ffmpeg.exe
  ffprobe.exe
  *.dll (all runtime dependencies required by ffmpeg/ffprobe)
```

**Recommended — use the build script** (PowerShell 5.1+, auto-detects VS/CMake):

```powershell
# Incremental build (most common)
.\scripts\windows_build.ps1

# Clean build (wipes build-msvc and reconfigures)
.\scripts\windows_build.ps1 -Clean

# Debug build
.\scripts\windows_build.ps1 -Config Debug

# Show all options
.\scripts\windows_build.ps1 -Help
```

Or via the `.bat` launcher (works from CMD and Explorer):

```bat
scripts\windows_build.bat
scripts\windows_build.bat -Clean
scripts\windows_build.bat -Config Debug
```

Manual build from **x64 Native Tools Command Prompt for VS 2022**:

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --target windows_cli --config Release
```

Output folder:
- `build-msvc/src/cli/Release/`
- Contains `ffmpeg_converter.exe` plus copied bundled `ffmpeg.exe`, `ffprobe.exe`, and DLL dependencies.

### Free Pascal (Linux and Windows)

```bash
# Linux CLI
make -C fpc/build cli
# → fpc/cli/ffmpeg_converter

# Linux GUI app bundle (self-contained)
make -C fpc/build gui-app
# → fpc/gui/form.app

# Windows CLI (via FPC compiler)
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json -Fu./fpc/cli \
  ./fpc/cli/ffmpeg_converter_windows.lpr -offmpeg_converter_windows.exe

# Windows GUI (via Lazarus)
lazbuild fpc/gui/form.lpi
# → fpc/gui/ffmpeg_converter_gui.exe
```

**Note**: Pascal macOS implementation discontinued; use C/CMake native GUI instead.

---

## Usage

```bash
# CLI examples
./build/bin/ffmpeg_converter input.mov
./build/bin/ffmpeg_converter -c prores_ks -p hq -a loudnorm2 -g rock input.mov
./build/bin/ffmpeg_converter -c mux --video-track replacement.hevc input.mkv
./build/bin/ffmpeg_converter -o /tmp/out -c hevc_videotoolbox input.mov  # macOS
ffmpeg_converter.exe -c mux --video-track replacement.hevc input.mkv     # Windows
```

CLI notes:
- Input files are positional arguments (`file1 file2 ...`).
- `-o/--output` sets an output directory (not a filename).
- If output directory is not set, converter uses default `$HOME/ffmpeg_converter`
  and creates it if missing.

GUI:
- **Linux (C)**: `./build/bin/ffmpeg_converter_gui` or AppImage: `./build/bin/ffmpeg_converter_gui-x86_64.AppImage`
- **Linux (Pascal)**: `./fpc/bin/ffmpeg_converter_gui` or AppImage: `./fpc/bin/ffmpeg_converter_gui_fpc-x86_64.AppImage`
- **macOS (C)**: `open build/install/ffmpeg_converter_gui_macos.app`
- **Windows (C CLI)**: `build-msvc/src/cli/Release/ffmpeg_converter.exe` (CLI only, most complete)
- **Windows (Pascal)**: GUI: `fpc/gui/ffmpeg_converter_gui.exe` or CLI: `fpc/cli/ffmpeg_converter_windows.exe`

---

## Project Structure

```
src/           C/CMake implementation
  converter/   Core conversion engine (converter.c, converter.h)
  cli/         Platform CLI entry points
  gui/         Linux GTK4 GUI
  gui_macos_native/  macOS Cocoa/AppKit GUI
  platform/    Platform-specific implementations
fpc/           Free Pascal implementation
  converter/   Pascal engine, C ABI export, Apple M4V creator
  common/      Reusable helpers (fs, path, process, time)
  json/        Loudnorm JSON parser
  cli/         Pascal CLI
  gui/         Lazarus/LCL GUI + form.app bundle
  build/       Makefile, package script
  test/        Unit tests and integration scripts
docs/          Install guides per platform
third_party/   Vendored jansson (C path)
```

---

## Documentation

- Install guides: [docs/install-linux.md](docs/install-linux.md),
  [docs/install-macos.md](docs/install-macos.md),
  [docs/install-windows.md](docs/install-windows.md)
- **Dependencies analysis**: [docs/DEPENDENCIES_ANALYSIS.md](docs/DEPENDENCIES_ANALYSIS.md) — complete reference for all libraries, codecs, filters, and GPU acceleration
- C architecture: [docs/PROJECT_DESCRIPTION.md](docs/PROJECT_DESCRIPTION.md)
- Native macOS install + behavior notes (C only): [docs/install-macos.md](docs/install-macos.md)
- Pascal port (Linux/Windows): [fpc/README.md](fpc/README.md)
- Pascal converter library: [fpc/converter/CONVERTER_LIBRARY_DETAIL.md](fpc/converter/CONVERTER_LIBRARY_DETAIL.md)
- C changelog: [CHANGELOG.md](CHANGELOG.md)
- Pascal changelog: [fpc/CHANGELOG.md](fpc/CHANGELOG.md)

---

## Notes

- `hevc_videotoolbox` uses Apple VideoToolbox hardware encoder on macOS. Bitrate
  is calculated automatically per-file using a sub-linear formula (base 35 Mbps
  at 4K/24 fps), clamped to [2000, 80000] kbps.
- Linux hardware codecs are runtime-detected and shown only when the active system
  and driver expose working VAAPI H.264 or HEVC encode.
- Linux `mux` mode is a one-source-file workflow that keeps processed audio and
  replaces the final video through `mkvmerge`.
- Windows `mux` mode uses the same `mkvmerge`-based pipeline. The `mux` codec
  appears in the menu and is accepted on the command line only when `mkvmerge`
  is detected at startup (PATH, env var `MKVMERGE`/`MKVMERGE_BIN`, or bundled).
- Linux GTK Apple M4V creator is GUI-only and now uses `libfdk_aac -vbr 5`
  by default for the AAC track.
- `prores_videotoolbox` uses Apple's proprietary ProRes encoder (hardware on
  Apple Silicon, software fallback on Intel via `-allow_sw 1`).
- Loudness 2-pass requires `ffmpeg` and `jansson`.
- The macOS native C `.app` bundle includes ffmpeg/ffprobe and attempts to bundle
  MP4Box + dependent dylibs at build time.
- macOS Pascal support discontinued in v2.4; use native C GUI instead.
- Bundled ffmpeg/ffprobe targets Intel x86_64; runs via Rosetta 2 on Apple Silicon on macOS.
- Windows MSVC CLI requires bundled ffmpeg/ffprobe payload in `src/platform/windows/bin/`; build copies that directory content next to the generated `.exe`.
- Windows Pascal GUI/CLI auto-detect Vulkan devices for GPU-accelerated ProRes encoding.

---

## License

MIT. See [LICENSE](LICENSE).
