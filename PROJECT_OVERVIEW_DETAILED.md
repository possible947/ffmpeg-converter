# ffmpeg_converter Project Detailed Overview

This overview reflects the repository as-is and avoids historical/obsolete notes.

## 1. Purpose

ffmpeg_converter orchestrates external `ffmpeg`/`ffprobe` for conversion and
normalization workflows, exposed via CLI and GUI frontends.

## 2. Top-Level Architecture

1. C/CMake path (`src/`)
2. Free Pascal path (`fpc/`)

Both implement similar conversion options and callback-driven progress flow.

## 3. C Path (`src/`)

### 3.1 Main components

- `src/converter/`: core conversion engine and public C API
- `src/cli/`: Linux/macOS/Windows CLI entry points
- `src/gui/`: Linux GTK4 GUI
- `src/gui_macos_native/`: native macOS Cocoa GUI
- `src/platform/`: platform progress implementations

### 3.2 Core converter behavior

In `src/converter/converter.c`:
- Validates input files
- Performs optional 2-pass analysis (peak/loudnorm)
- Builds ffmpeg command
- Tracks encode progress from `-progress pipe:1`
- Preflights output directory before processing:
  - uses configured output dir when provided
  - otherwise defaults to `$HOME/ffmpeg_converter`
  - creates missing directory
  - validates writable directory

### 3.3 C CLI semantics

- Usage style: positional input files
- `-o/--output`: output directory
- No `--dry-run` option in current implementation

## 4. Native macOS C GUI and Apple M4V

Native GUI (`src/gui_macos_native`) includes:
- standard conversion via converter bridge
- Apple M4V creator backend and UI integration

Apple M4V modes:
- direct (`input -> .m4v`)
- edit-before-mux (`converter output -> .m4v -> cleanup`)

Bundling flow includes ffmpeg/ffprobe and attempts MP4Box + dependent dylibs.

## 5. Pascal Path (`fpc/`)

### 5.1 Main components

- `fpc/converter/`: engine + C ABI + Apple M4V creator
- `fpc/cli/`: CLI frontend
- `fpc/gui/`: Lazarus/LCL GUI
- `fpc/build/`: Makefile + packaging scripts
- `fpc/test/`: unit/integration tests

### 5.2 Output handling parity

Pascal path includes explicit output preflight utilities/tests and follows the
same default output directory convention (`$HOME/ffmpeg_converter`).

## 6. Build Matrix

### 6.1 C/CMake targets

- Linux: `linux_cli`, `linux_gui`
- macOS: `macos_cli`, `macos_gui_native`

### 6.2 Pascal targets (`make -C fpc/build`)

- `cli`, `lib`, `tests`, `gui`, `gui-app`

## 7. Dependencies

- Core runtime tools: `ffmpeg`, `ffprobe`
- C loudnorm parsing: `jansson`
- Linux C GUI: GTK4
- Apple M4V workflows: `MP4Box`

## 8. Practical References

- Main project guide: `README.md`
- Install guides: `docs/install-linux.md`, `docs/install-macos.md`,
  `docs/install-windows.md`
- Native macOS Apple M4V details: `docs/macos-native-apple-m4v-design.md`
