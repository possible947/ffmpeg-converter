# ffmpeg_converter Project Detailed Overview

## 1. Project Purpose
`ffmpeg_converter` is a cross-platform media conversion project that orchestrates
external `ffmpeg` and `ffprobe` runs for batch video/audio processing. It exposes
a clean API for building conversion pipelines and wraps them in both CLI and GUI
front-ends.

## 2. High-Level Architecture
1. **C implementation** (`src/`) — original converter engine, Linux GTK4 GUI, native
   macOS Cocoa/AppKit GUI. Built with CMake from the repository root.
2. **Free Pascal implementation** (`fpc/`) — full FPC port with converter engine,
   shared library (C ABI), CLI, and Lazarus/LCL GUI. Built with `fpc/build/Makefile`
   and `lazbuild`.
3. Both implementations expose the same converter API concept and produce compatible
   command-line behaviour.

## 3. Root-Level Components
- `CMakeLists.txt`: top-level C/CMake build entry.
- `README.md`: user-facing project description and quick build/use guide.
- `CHANGELOG.md`: C/CMake implementation release history.
- `WINDOWS_BRANCH.md`: Windows planning notes (future work).
- `mux_apple_mp4.sh`, `mux_apple_m4v.sh`: shell helpers for muxing workflows.
- `third_party/jansson/`: vendored C JSON library for loudnorm support.
- `build/`: generated CMake build directory (git-ignored).
- `fpc/`: Pascal port workspace.
- `src/`: original C source tree.

## 4. C Source Tree Components (`src/`)
- `src/converter/`: core C converter library (`converter.c`, `converter.h`, API spec).
- `src/cli/`: platform-specific C CLI entry points, argument/menu handling.
- `src/gui/`: Linux GTK4 GUI — app, callbacks, threading, window composition.
- `src/gui_macos_native/`: macOS native Cocoa/AppKit GUI — `AppDelegate`, main
  window controller, progress display, drag-and-drop integration.
- `src/platform/`: platform-specific implementations (async progress, platform glue).
- `src/progress/`: progress interface (percent, FPS, ETA) used by CLI and engine.
- `src/audio/`, `src/video/`, `src/core/`, `src/utils/`, `src/ffmpeg_cmd/`:
  modular headers and helpers for command-building and processing flows.

## 5. Pascal Source Tree Components (`fpc/`)
- `fpc/README.md`: Pascal workspace overview and build instructions.
- `fpc/CHANGELOG.md`: Pascal implementation release history.
- `fpc/DESCRIPTION.md`: Pascal port scope and verification commands.
- `fpc/build/Makefile`: CLI, shared library, and test build targets.
- `fpc/build/package_macos_app.sh`: script to package `form.app` with bundled
  `ffmpeg`, `ffprobe`, and `MP4Box`.
- `fpc/common/`: reusable helpers — filesystem, path, process execution, time.
- `fpc/json/`: loudnorm two-pass JSON parser.
- `fpc/cli/`: Pascal CLI program, callbacks, menu flow, argument parsing.
- `fpc/converter/`: Pascal converter engine (`converter_core.pas`), C ABI export
  (`converter_pas.lpr`), Apple M4V creator (`apple_m4v_creator.pas`).
- `fpc/gui/`: Lazarus/LCL GUI (`form.pas`, `form.lfm`, `form.lpi`).
  Packaged as `form.app` — self-contained macOS `.app` bundle.
- `fpc/test/`: unit tests for command-building, path resolution, and analysis.

## 6. Build Outputs

### C/CMake targets (configured via CMake)
| Target             | Platform | Output                                              |
|--------------------|----------|----------------------------------------------------|
| `linux_cli`        | Linux    | `build/src/cli/ffmpeg_converter`                   |
| `linux_gui`        | Linux    | `build/src/gui/ffmpeg_converter_gui` (GTK4)        |
| `macos_cli`        | macOS    | `build/src/cli/ffmpeg_converter`                   |
| `macos_gui_native` | macOS    | installed to `build/install/ffmpeg_converter_gui_macos.app` |

### Pascal targets
| Target               | Output                                         |
|----------------------|------------------------------------------------|
| CLI (`make -C fpc/build cli`) | `fpc/cli/ffmpeg_converter`         |
| Shared library       | `fpc/converter/libconverter_pas.so`            |
| GUI (`lazbuild`)     | `fpc/gui/ffmpeg_converter_gui` (binary)        |
| App bundle           | `fpc/gui/form.app` (self-contained macOS app)  |

## 7. Runtime Dependencies

| Component              | Dependency                                |
|------------------------|-------------------------------------------|
| All conversion paths   | `ffmpeg`, `ffprobe` (PATH or bundled)     |
| Loudnorm two-pass (C)  | `jansson` library                         |
| Linux GUI (C)          | GTK4 (`libgtk-4`)                         |
| macOS GUI (C)          | macOS 12+ AppKit (no GTK needed)          |
| Pascal GUI / app bundle| Bundled `ffmpeg`, `ffprobe`, `MP4Box`     |
| Apple M4V creation     | `MP4Box` from GPAC                        |

The macOS Pascal `.app` bundle is fully self-contained — no system `ffmpeg` or
`MP4Box` installation is required to run it.

## 8. Converter Library Summary
- C library: `src/converter/converter.c` (`converter.h` public API).
- Pascal library: `fpc/converter/converter_core.pas` (engine) +
  `fpc/converter/converter_pas.lpr` (C ABI export).
- API concept (both): create → set callbacks → set options → process files → stop.
- Error mapping: `converter_error_to_string()`.
- See [fpc/converter/CONVERTER_LIBRARY_DETAIL.md](fpc/converter/CONVERTER_LIBRARY_DETAIL.md) for full Pascal library breakdown.

## 9. Platform Install/Build Reference
- Linux: [docs/install-linux.md](docs/install-linux.md)
- macOS: [docs/install-macos.md](docs/install-macos.md)
- Windows: [docs/install-windows.md](docs/install-windows.md)
