# Free Pascal Port Scaffold

This folder contains the Free Pascal port scaffold for `ffmpeg-converter`.

## Goals

- Mirror the C converter API from `src/converter/converter.h`
- Keep conversion engine behavior close to C version
- Provide a Pascal-first CLI implementation
- Leave room for a later GUI implementation (Lazarus/LCL recommended)

## Folder Layout

- `converter/`: API-compatible core units
- `common/`: reusable file, process, and time helpers
- `json/`: loudnorm JSON parsing helpers
- `cli/`: command line app and interactive menu stubs
- `test/`: basic test stubs for parity checks
- `build/`: FPC build helper files

## Current Status

Pascal port is actively usable for CLI workflows and test harness runs.
Core conversion flow, overwrite handling, Apple M4V helper path, and 2-pass audio analysis paths are implemented.
Non-GUI library port is included and can be built as a shared library.

## Recent Changes (0.2.0)

- Fixed process output capture reliability in `common/process_utils.pas` by waiting for process completion.
- Fixed `peak2` and `loudnorm2` analysis stability by hardening parser flow and aligning `loudnorm2` to JSON-based extraction.
- Added explicit overwrite flags (`-y` / `-n`) in command builder for deterministic overwrite behavior.
- Updated GUI Apple M4V list flow behavior (direct mode and edit-chain mode) and overwrite handling.
- Added local parity tests and helper checks in `fpc/test/`.
- Cleaned generated artifacts from source tree and added `fpc/.gitignore` for FPC/Lazarus build outputs.

## Build

From repository root:

```bash
make -C fpc/build cli
make -C fpc/build lib
make -C fpc/build tests
```

Generated artifacts:

- CLI binary: `fpc/cli/ffmpeg_converter`
- Shared library: `fpc/converter/libconverter_pas.so`

## C/C++ Integration

Use header: `fpc/converter/converter_pas.h`

Link against:

```bash
-L fpc/converter -lconverter_pas
```

Runtime loader path example:

```bash
LD_LIBRARY_PATH=fpc/converter ./your_app
```

Detailed converter library description: `fpc/converter/CONVERTER_LIBRARY_DETAIL.md`.

Cross-platform install/build guides: `docs/install-linux.md`, `docs/install-macos.md`, `docs/install-windows.md`.

## Next Implementation Steps

1. Port command building logic from `src/converter/converter.c`
2. Port peak/loudnorm analysis flow using external `ffmpeg`/`ffprobe`
3. Port file verification and valid-file compaction behavior in CLI
4. Add command parity tests for representative option combinations
