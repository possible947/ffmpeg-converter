# ffmpeg_converter Project Detailed Overview

## 1. Project Purpose
`ffmpeg_converter` is a cross-platform media conversion project that orchestrates external `ffmpeg` and `ffprobe` runs for batch video/audio processing.

## 2. High-Level Architecture
1. C implementation (`src/`) provides original converter engine, CLI, and GTK GUI.
2. Free Pascal implementation (`fpc/`) provides non-GUI converter engine, CLI, tests, and shared library export.
3. Build systems are split by language:
4. C uses CMake from repository root.
5. Pascal uses `fpc/build/Makefile` and optional `fpc/build/fpmake.pp`.

## 3. Root-Level Components
- `CMakeLists.txt`: top-level C build entry.
- `README.md`: user-facing project description and quick build/use notes.
- `PROJECT_DESCRIPTION.md`: additional project notes.
- `WINDOWS_BRANCH.md`: Windows-related planning notes.
- `mux_apple_mp4.sh`: helper shell script for muxing workflows.
- `third_party/jansson/`: vendored C JSON dependency for C converter path.
- `build/`: generated CMake build directory.
- `fpc/`: Pascal port workspace.
- `src/`: original C source tree.

## 4. C Source Tree Components (`src/`)
- `src/converter/`: core C converter library (`converter.c`, `converter.h`, API specification).
- `src/cli/`: platform-specific C CLI entry points and argument/menu handling.
- `src/gui/`: GTK4 GUI app, callbacks, threading, and window composition.
- `src/platform/`: platform-specific implementations (progress and platform glue).
- `src/progress/`: progress interface used by C CLI/engine.
- `src/audio/`, `src/video/`, `src/core/`, `src/utils/`, `src/ffmpeg_cmd/`: modular headers and helpers participating in command-building and processing flow.
- `src/ffmpeg_convert.glade`: GUI-related design resource.

## 5. Pascal Source Tree Components (`fpc/`)
- `fpc/README.md`: Pascal workspace overview and build instructions.
- `fpc/DESCRIPTION.md`: Pascal port scope and verification commands.
- `fpc/build/Makefile`: practical build entry for CLI, shared library, tests.
- `fpc/build/fpmake.pp`: package metadata and optional fpmake flow.
- `fpc/common/`: reusable filesystem, path, process, and time utilities.
- `fpc/json/`: loudnorm JSON parsing module.
- `fpc/cli/`: Pascal CLI program, callbacks, menu flow, argument parsing.
- `fpc/converter/`: Pascal converter engine and C ABI export surface.
- `fpc/test/`: focused tests for command-building and path behavior.

## 6. Build Outputs
- C CLI targets: `linux_cli`, `macos_cli`, `windows_cli` (via CMake conditions).
- C GUI targets: `linux_gui`, `macos_gui` when GTK is enabled and available.
- Pascal CLI binary: `fpc/cli/ffmpeg_converter`.
- Pascal shared library: `fpc/converter/libconverter_pas.so`.

## 7. Runtime Dependencies
- Required for all conversion paths: external `ffmpeg`, `ffprobe` in runtime environment.
- C converter path requires `jansson` library.
- C GUI path requires GTK4 stack.
- Pascal path uses FPC runtime and process execution; no GTK dependency in current non-GUI port.

## 8. Converter Library Summary
- Original converter library: `src/converter/converter.c`.
- Pascal converter library: `fpc/converter/converter_core.pas` plus `fpc/converter/converter_pas.lpr` export library.
- Common API concept: create converter, set callbacks, set options, process files, stop, map error to string.
- See `fpc/converter/CONVERTER_LIBRARY_DETAIL.md` for full library component breakdown.

## 9. Platform Install/Build Reference
- Linux commands: `docs/install-linux.md`.
- macOS commands: `docs/install-macos.md`.
- Windows commands: `docs/install-windows.md`.
