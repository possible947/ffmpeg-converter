# C Implementation (src)

This directory contains the C/CMake implementation of ffmpeg_converter.

## Overview

C path provides:
- Core converter engine (`src/converter/`).
- CLI frontends (`src/cli/linux`, `src/cli/macos`, `src/cli/windows`).
- Linux GTK4 GUI (`src/gui/`).
- Native macOS Cocoa GUI (`src/gui_macos_native/`).

## Build Targets

From repository root:

```bash
cmake -B build
```

Linux:

```bash
cmake --build build --target linux_cli
cmake --build build --target linux_gui
```

macOS:

```bash
cmake --build build --target macos_cli
cmake --build build --target macos_gui_native
cmake --install build
```

Notes:
- Linux GUI target is controlled by `ENABLE_LINUX_GUI`.
- Native macOS GUI target is controlled by `ENABLE_MACOS_NATIVE_GUI`.

## CLI Behavior (Current)

Usage:

```bash
ffmpeg_converter [options] file1 file2 ...
```

Supported options:
- `-c, --codec <copy|prores|prores_ks|h265_mi50>`
- `-p, --profile <lt|standard|hq|4444>`
- `-d, --deblock <none|weak|strong>`
- `-a, --audio-norm <none|peak|peak2|loudnorm|loudnorm2>`
- `-g, --genre <edm|rock|hiphop|classical|podcast>`
- `--overwrite`
- `-o, --output <directory>`
- `-h, --help`

Important:
- Inputs are positional files, not `--input` pairs.
- `-o/--output` sets output directory, not a single output filename.
- If output directory is not set, converter uses default `$HOME/ffmpeg_converter`
  and creates it when missing.

## Converter Engine Notes

`src/converter/converter.c`:
- Validates input files.
- Runs optional peak/loudnorm 2-pass analysis.
- Builds ffmpeg command line.
- Runs ffmpeg with `-progress pipe:1` and parses percent/fps/eta.
- Performs output directory preflight (default dir fallback, create, writable check).

## macOS Native GUI Notes

`src/gui_macos_native` supports:
- Standard conversion flow through converter bridge.
- Apple M4V creator flow:
  - Direct mode (`input -> .m4v`).
  - Edit-before-mux mode (`converter output -> .m4v -> cleanup`).

At runtime it resolves/bundles:
- `ffmpeg`, `ffprobe`.
- `MP4Box` (for Apple M4V).

## Known Non-Goals in C Path

- No `--dry-run` option in current C CLI.
- Windows GUI is not implemented in C path.
