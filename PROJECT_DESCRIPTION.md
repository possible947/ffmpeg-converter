# ffmpeg_converter — Developer Description

This document describes the current, factual state of the repository.
It is intentionally concise and aligned with the code and build files.

## 1. Project Scope

ffmpeg_converter is a cross-platform media conversion project with two
implementations:

- C/CMake implementation in `src/`
- Free Pascal implementation in `fpc/`

Both paths provide conversion workflows around external `ffmpeg`/`ffprobe`.

## 2. Implementations

### 2.1 C/CMake (`src/`)

- Core engine: `src/converter/converter.c`, `src/converter/converter.h`
- Linux CLI: `src/cli/linux/main.c`
- macOS CLI: `src/cli/macos/main.c`
- Linux GUI (GTK4): `src/gui/`
- macOS native GUI (Cocoa/AppKit): `src/gui_macos_native/`

Current C feature set:
- Codecs: `copy`, `prores`, `prores_ks`, `h265_mi50`
- Audio normalization: `none`, `peak_norm`, `peak_norm_2pass`,
  `loudness_norm`, `loudness_norm_2pass`
- Encode progress parsing from `ffmpeg -progress pipe:1`
- Output directory preflight in converter core:
  - if not set, default is `$HOME/ffmpeg_converter`
  - directory is created when missing
  - writable check is enforced

Current C CLI behavior:
- Input files are positional: `ffmpeg_converter [options] file1 file2 ...`
- `-o/--output` sets output directory (not output file path)
- No `--dry-run` option in current C CLI

### 2.2 Free Pascal (`fpc/`)

- Engine: `fpc/converter/converter_core.pas`
- C ABI export: `fpc/converter/converter_pas.lpr`
- CLI: `fpc/cli/`
- GUI: `fpc/gui/`
- Packaging/scripts/tests: `fpc/build/`, `fpc/test/`

Pascal path includes output preflight tests and Apple M4V creator support.

## 3. Apple M4V Status

Apple M4V workflow is implemented in:

- Pascal: `fpc/converter/apple_m4v_creator.pas`
- C native macOS GUI:
  - `src/gui_macos_native/apple_m4v_creator.m`
  - bridge/UI integration in `src/gui_macos_native/converter_bridge.m`
    and `src/gui_macos_native/main.m`

C native macOS GUI supports:
- Direct mode (`input -> .m4v`)
- Edit-before-mux mode (`converter output -> .m4v -> cleanup`)

## 4. Build Targets

### 4.1 C/CMake targets

- Linux:
  - `linux_cli`
  - `linux_gui`
- macOS:
  - `macos_cli`
  - `macos_gui_native`

GUI target switches:
- `ENABLE_LINUX_GUI`
- `ENABLE_MACOS_NATIVE_GUI`

### 4.2 Pascal Make targets

In `fpc/build/Makefile`:
- `cli`
- `lib`
- `tests`
- `gui`
- `gui-app`

## 5. Runtime Dependencies

C path:
- `ffmpeg`, `ffprobe`
- `jansson` (system library)
- Linux GUI: GTK4
- macOS native GUI: AppKit (no GTK)

Pascal path:
- FPC/Lazarus for builds
- `ffmpeg`, `ffprobe`
- `MP4Box` for Apple M4V creation/packaging flows

## 6. Known Boundaries

- Windows C GUI is not implemented.
- C CLI currently does not support `--dry-run`.

## 7. Canonical References

- User-facing overview: `README.md`
- C changelog: `CHANGELOG.md`
- Pascal changelog: `fpc/CHANGELOG.md`
- Install guides: `docs/install-linux.md`, `docs/install-macos.md`,
  `docs/install-windows.md`
- Apple M4V design/status: `docs/macos-native-apple-m4v-design.md`
