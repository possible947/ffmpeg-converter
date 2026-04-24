# ffmpeg_converter 2.2

Cross-platform media conversion and mux tool with CLI and GUI for building and
running optimized `ffmpeg` workflows. Version 2.2 adds Linux runtime-probed
VAAPI, MKV post-mux mode, and a Linux GTK Apple M4V creator workflow.

Two independent implementations share the same conversion logic and CLI behavior:

- **C/CMake** (`src/`) — original engine, Linux GTK4 GUI, native macOS Cocoa GUI.
- **Free Pascal** (`fpc/`) — full FPC port with CLI, shared library, and
  Lazarus/LCL GUI for macOS (self-contained `.app` bundle, icon aligned with C macOS app).

## macOS v2.2 Highlights

- Native macOS GUI now supports `mux` workflow with replacement video track
  and final `.mkv` output via `mkvmerge`.
- Audio output modes in macOS native GUI are aligned with Linux:
  `pcm`, `fdk_aac_q5`, `fdk_aac_q5_ac3_640`.
- Apple M4V creator performs source codec preflight and accepts only
  `h264`, `hevc`, `prores` without re-encode in this path.
- AAC encoder selection for M4V and converter audio now uses runtime fallback:
  `aac_at` -> `libfdk_aac` -> `aac`.
- ProRes profile handling is hardened: invalid or missing profile defaults to
  `standard`; `prores_ks` uses explicit profile names (`lt`, `standard`, `hq`).
- Native `.app` bundling includes `ffmpeg`/`ffprobe` and attempts to bundle
  `mkvmerge` (with runtime fallback to `MKVMERGE_BIN` or PATH lookup).

## Linux v2.2 Highlights

- Linux GUI: GTK4 with audio normalization, mux mode, and Apple M4V creator.
- Runtime VAAPI codec probing: `h264_vaapi` and `hevc_vaapi` appear only when
  the active system/driver supports them.
- Optional **AppImage packaging** via CMake `ENABLE_APPIMAGE` option and
  `package_appimage` target. Produces a single portable executable (~71 MB)
  bundling the GUI, ffmpeg/ffprobe, mkvmerge, MP4Box, and required non-system
  shared libraries. Run `cmake --build . --target package_appimage` after
  enabling the option.

---

## Features

- Video codecs (cross-platform): `copy`, `prores`, `prores_ks`.
- Linux runtime-probed video codecs: `h264_vaapi`, `hevc_vaapi`.
- Video codecs (macOS VideoToolbox): `prores_videotoolbox`, `hevc_videotoolbox`.
- Audio normalization: `none`, `peak`, `peak 2-pass`, `loudness`, `loudness 2-pass`.
- Audio output modes: PCM, FDK AAC q5, FDK AAC q5 + AC3 640.
- Linux MKV mux mode: one source file + external replacement video track, final output via `mkvmerge`.
- **Audio filter multithreading**: 2-pass analysis uses `-filter_threads N` (N = CPU/2) for parallel audio processing.
- Encode progress: percent, FPS, ETA.
- CLI with argument parsing and interactive menu.
- **Linux GUI** — GTK4 (C implementation). Build produces `linux_gui` binary; optional AppImage packaging available via `ENABLE_APPIMAGE=ON` and `package_appimage` target (produces single-file portable AppImage).
- **macOS GUI** — native Cocoa/AppKit, self-contained `.app` bundle with bundled
  `ffmpeg`, `ffprobe`, and `MP4Box` (C native implementation).
- Linux GTK Apple M4V creator: dedicated GUI-only workflow matching the macOS direct M4V path.
- Apple M4V creator: multi-step pipeline (video copy + AAC + AC3 + MP4Box mux
  + optional chapter import) in both Pascal GUI and C native macOS GUI.

---

## Requirements

### C/CMake path
- `cmake` ≥ 3.16, C compiler (clang/gcc).
- `jansson` library (JSON parsing for loudnorm).
- `ffmpeg` + `ffprobe`:
  - Linux: staged next to CLI/GUI in `build/bin` when available from `src/platform/linux/bin/`.
  - macOS: bundled inside native `.app` from `src/platform/macos/bin/`.
  - Windows (MSVC): required in `src/platform/windows/bin/` (`ffmpeg.exe`, `ffprobe.exe`, and their DLL dependencies); copied next to `ffmpeg_converter.exe` at build time.
  On macOS, priority order: macports FFmpeg8 (`/opt/local/bin/ffmpeg8`) → macports (`/opt/local/bin/ffmpeg`) → system PATH.
- `MP4Box` (GPAC) for Apple M4V packaging/runtime on macOS native GUI and Linux GTK M4V workflow.
- `mkvmerge` for Linux mux mode.
- Linux GUI only: `libgtk-4-dev` (or distro equivalent).
- Optional AppImage packaging: `appimagetool` (https://github.com/AppImage/AppImageKit).

### Free Pascal path
- Lazarus IDE + FPC (for GUI), or plain `fpc` compiler (for CLI/library).
- macOS packaging: `MP4Box` from GPAC — `sudo port install gpac`.
- `ffmpeg` + `ffprobe` static binaries placed in `src/platform/macos/bin/`
  for bundling.
- `mkvmerge` for Pascal `mux` workflow (`codec=mux`) and GUI post-mux stage.

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
cmake -DENABLE_APPIMAGE=ON ..
cmake --build . --target package_appimage
# Output: src/gui/ffmpeg_converter_gui-x86_64.AppImage
```
Requires `appimagetool` in PATH. The script `src/gui/package_appimage.sh` can
also be invoked directly.

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

Build from repository root in **x64 Native Tools Command Prompt for VS 2022**:

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --target windows_cli --config Release
```

Output folder:
- `build-msvc/src/cli/Release/`
- Contains `ffmpeg_converter.exe` plus copied bundled `ffmpeg.exe`, `ffprobe.exe`, and DLL dependencies.

### Free Pascal — macOS

```bash
# CLI
make -C fpc/build cli

# GUI binary
lazbuild fpc/gui/form.lpi

# Package into self-contained .app (bundles ffmpeg, ffprobe, MP4Box, icon)
make -C fpc/build gui-app
# → fpc/gui/form.app
```

---

## Usage

```bash
# CLI examples
./build/bin/ffmpeg_converter input.mov
./build/bin/ffmpeg_converter -c prores_ks -p hq -a loudnorm2 -g rock input.mov
./build/bin/ffmpeg_converter -c mux --video-track replacement.hevc input.mkv
./build/bin/ffmpeg_converter -o /tmp/out -c hevc_videotoolbox input.mov  # macOS
```

CLI notes:
- Input files are positional arguments (`file1 file2 ...`).
- `-o/--output` sets an output directory (not a filename).
- If output directory is not set, converter uses default `$HOME/ffmpeg_converter`
  and creates it if missing.

GUI:
- **Linux**: `./build/bin/ffmpeg_converter_gui` or the AppImage:
  `src/gui/ffmpeg_converter_gui-x86_64.AppImage`
- **macOS native**: `open build/install/ffmpeg_converter_gui_macos.app`
- **macOS Pascal**: `open fpc/gui/form.app`
- **Windows CLI**: `build-msvc/src/cli/Release/ffmpeg_converter.exe`

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
- C developer description: [docs/PROJECT_DESCRIPTION.md](docs/PROJECT_DESCRIPTION.md)
- Native macOS install + behavior notes: [docs/install-macos.md](docs/install-macos.md)
- Pascal port: [fpc/README.md](fpc/README.md)
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
- Linux GTK Apple M4V creator is GUI-only and now uses `libfdk_aac -vbr 5`
  by default for the AAC track.
- `prores_videotoolbox` uses Apple's proprietary ProRes encoder (hardware on
  Apple Silicon, software fallback on Intel via `-allow_sw 1`).
- Loudness 2-pass requires `ffmpeg` and `jansson`.
- The macOS native C `.app` bundle includes ffmpeg/ffprobe and attempts to bundle
  MP4Box + dependent dylibs at build time.
- The macOS Pascal `.app` bundle is fully self-contained — no system ffmpeg needed.
- The Pascal `.app` bundle now includes `icon.icns` (imported from C macOS GUI resources).
- Bundled ffmpeg/ffprobe targets Intel x86_64; runs via Rosetta 2 on Apple Silicon.
- Windows MSVC CLI requires bundled ffmpeg/ffprobe payload in `src/platform/windows/bin/`; build copies that directory content next to the generated `.exe`.

---

## License

MIT. See [LICENSE](LICENSE).
