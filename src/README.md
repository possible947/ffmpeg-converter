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
- `-c, --codec <copy|prores|prores_ks|mux|h264_vaapi|hevc_vaapi>`
- `-p, --profile <lt|standard|hq|4444>`
- `-d, --deblock <none|weak|strong>`
- `-a, --audio-norm <none|peak|peak2|loudnorm|loudnorm2>`
- `--audio-output <pcm|fdk_aac_q5|fdk_aac_q5_ac3_640>`
- `--video-track <file>` for `codec=mux`
- `-g, --genre <edm|rock|hiphop|classical|podcast>`
- `--overwrite`
- `-o, --output <directory>`
- `-h, --help`

Important:
- Inputs are positional files, not `--input` pairs.
- `-o/--output` sets output directory, not a single output filename.
- If output directory is not set, converter uses default `$HOME/ffmpeg_converter`
  and creates it when missing.
- On Linux, hardware codecs are exposed only when runtime probing confirms
  `h264_vaapi` or `hevc_vaapi` on an accessible render node.
- On Linux, `codec=mux` is a one-source-file workflow that requires
  `--video-track <file>` and produces final `.mkv` output through `mkvmerge`.

## Converter Engine Notes

`src/converter/converter.c`:
- Validates input files.
- Runs optional peak/loudnorm 2-pass analysis.
- Builds ffmpeg command line.
- Runs ffmpeg with `-progress pipe:1` and parses percent/fps/eta.
- Performs output directory preflight (default dir fallback, create, writable check).

Linux runtime prefers bundled tools when present:
- executable-adjacent `ffmpeg`
- executable-adjacent `ffprobe`
- executable-adjacent `mkvmerge`
- development fallback from `src/platform/linux/bin/ffmpeg`
- development fallback from `src/platform/linux/bin/ffprobe`

If bundled tools are missing, Linux falls back to environment variables and then `PATH`.

## Linux GTK Notes

`src/gui` now supports two Linux-only workflows:
- Standard converter flow through the shared converter engine.
- GTK-only Apple M4V creator flow.

Linux Apple M4V creator behavior:
- Exposed only in GTK GUI, not in CLI.
- Runs as a separate action, not as `codec=m4v`.
- Uses current file list as input.
- Uses `ffmpeg`, `ffprobe`, and `MP4Box` directly.
- Follows the same direct mux sequence as macOS Apple M4V creator:
  - video copy
  - AAC encode
  - AC3 encode
  - MP4Box mux
  - optional chapters import
- Input preflight currently allows only `h264`, `hevc`, or `prores` video streams.

## macOS Native GUI Notes

`src/gui_macos_native` supports:
- Standard conversion flow through converter bridge.
- Apple M4V creator flow:
  - Direct mode (`input -> .m4v`).
  - Edit-before-mux mode (`converter output -> .m4v -> cleanup`).

At runtime it resolves/bundles:
- `ffmpeg`, `ffprobe`.
- `MP4Box` (for Apple M4V).

Linux builds now place the CLI, GTK GUI, `ffmpeg`, and `ffprobe` together in `build/bin`
so the toolset can be copied or symlinked as a single folder on the same machine.

## Known Non-Goals in C Path

- No `--dry-run` option in current C CLI.
- Windows GUI is not implemented in C path.
