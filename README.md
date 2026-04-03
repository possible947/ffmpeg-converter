# ffmpeg_converter

Cross-platform media conversion tool with CLI and GUI for building and running
optimized `ffmpeg` commands. Supports ProRes, stream copy, H.265 VAAPI, and
multiple audio normalization modes including two-pass EBU R128 (loudnorm).

Two independent implementations share the same conversion logic and CLI behavior:

- **C/CMake** (`src/`) — original engine, Linux GTK4 GUI, native macOS Cocoa GUI.
- **Free Pascal** (`fpc/`) — full FPC port with CLI, shared library, and
  Lazarus/LCL GUI for macOS (self-contained `.app` bundle).

---

## Features

- Video codecs: `copy`, `prores`, `prores_ks`, `h265_mi50` (H.265 VAAPI).
- Audio normalization: `none`, `peak`, `peak 2-pass`, `loudness`, `loudness 2-pass`.
- **Audio filter multithreading**: 2-pass analysis uses `-filter_threads N` (N = CPU/2) for parallel audio processing.
- Encode progress: percent, FPS, ETA.
- CLI with argument parsing and interactive menu.
- **Linux GUI** — GTK4 (C implementation).
- **macOS GUI** — native Cocoa/AppKit, self-contained `.app` bundle with bundled
  `ffmpeg`, `ffprobe`, and `MP4Box` (C native implementation).
- Apple M4V creator: multi-step pipeline (video copy + AAC + AC3 + MP4Box mux
  + optional chapter import) in both Pascal GUI and C native macOS GUI.

---

## Requirements

### C/CMake path
- `cmake` ≥ 3.16, C compiler (clang/gcc).
- `jansson` library (JSON parsing for loudnorm).
- `ffmpeg` + `ffprobe` — bundled inside macOS app bundle; required in PATH for CLI.
  On macOS, priority order: macports FFmpeg8 (`/opt/local/bin/ffmpeg8`) → macports (`/opt/local/bin/ffmpeg`) → system PATH.
- `MP4Box` (GPAC) for Apple M4V packaging/runtime on macOS native GUI.
- Linux GUI only: `libgtk-4-dev` (or distro equivalent).

### Free Pascal path
- Lazarus IDE + FPC (for GUI), or plain `fpc` compiler (for CLI/library).
- macOS packaging: `MP4Box` from GPAC — `sudo port install gpac`.
- `ffmpeg` + `ffprobe` static binaries placed in `src/platform/macos/bin/`
  for bundling.

---

## Quick Build

### C/CMake — Linux

```bash
mkdir build && cd build
cmake ..
cmake --build . --target linux_cli
cmake --build . --target linux_gui
```

### C/CMake — macOS (native Cocoa GUI)

```bash
mkdir build && cd build
cmake ..
cmake --build . --target macos_cli
cmake --build . --target macos_gui_native
cmake --install .   # produces build/install/ffmpeg_converter_gui_macos.app
```

### Free Pascal — macOS

```bash
# CLI
make -C fpc/build cli

# GUI binary
lazbuild fpc/gui/form.lpi

# Package into self-contained .app (bundles ffmpeg, ffprobe, MP4Box)
bash fpc/build/package_macos_app.sh
# → fpc/gui/form.app
```

---

## Usage

```bash
# CLI examples
./build/src/cli/ffmpeg_converter input.mov
./build/src/cli/ffmpeg_converter -c prores_ks -p hq -a loudnorm2 -g rock input.mov
./build/src/cli/ffmpeg_converter -o /tmp/out -c h265_mi50 input.mov
```

CLI notes:
- Input files are positional arguments (`file1 file2 ...`).
- `-o/--output` sets an output directory (not a filename).
- If output directory is not set, converter uses default `$HOME/ffmpeg_converter`
  and creates it if missing.

GUI:
- **Linux**: `./build/src/gui/ffmpeg_converter_gui`
- **macOS native**: `open build/install/ffmpeg_converter_gui_macos.app`
- **macOS Pascal**: `open fpc/gui/form.app`

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
- C architecture: [PROJECT_OVERVIEW_DETAILED.md](PROJECT_OVERVIEW_DETAILED.md)
- C developer description: [PROJECT_DESCRIPTION.md](PROJECT_DESCRIPTION.md)
- Native macOS Apple M4V design/status: [docs/macos-native-apple-m4v-design.md](docs/macos-native-apple-m4v-design.md)
- Pascal port: [fpc/README.md](fpc/README.md)
- Pascal converter library: [fpc/converter/CONVERTER_LIBRARY_DETAIL.md](fpc/converter/CONVERTER_LIBRARY_DETAIL.md)
- C changelog: [CHANGELOG.md](CHANGELOG.md)
- Pascal changelog: [fpc/CHANGELOG.md](fpc/CHANGELOG.md)

---

## Notes

- `h265_mi50` requires VAAPI. Default device: `/dev/dri/renderD128`. Override
  via `VAAPI_DEVICE` environment variable.
- Loudness 2-pass requires `ffmpeg` and `jansson`.
- The macOS native C `.app` bundle includes ffmpeg/ffprobe and attempts to bundle
  MP4Box + dependent dylibs at build time.
- The macOS Pascal `.app` bundle is fully self-contained — no system ffmpeg needed.
- Bundled ffmpeg/ffprobe targets Intel x86_64; runs via Rosetta 2 on Apple Silicon.

---

## License

MIT. See [LICENSE](LICENSE).
