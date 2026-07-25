# Copilot Instructions for `ffmpeg-converter`

## Build, test, and lint commands

### C/CMake builds

#### Linux
```bash
cmake -S . -B build
cmake --build build --target linux_cli
cmake --build build --target linux_gui
```

Optional AppImage:
```bash
cmake -S . -B build -DENABLE_APPIMAGE=ON
cmake --build build --target package_appimage
```

#### macOS
```bash
cmake -S . -B build
cmake --build build --target macos_cli
cmake --build build --target macos_gui_native
cmake --install build
```

#### Windows (MSVC)
```powershell
.\scripts\windows_build.ps1
.\scripts\windows_build.ps1 -Clean
.\scripts\windows_build.ps1 -BuildFPC
.\scripts\windows_build.ps1 -BuildGUI
```

Precondition for `windows_cli`: place `ffmpeg.exe`, `ffprobe.exe`, and required DLLs in `src/platform/windows/bin/`.

### Free Pascal builds

#### Linux
```bash
make -C fpc/build cli
make -C fpc/build lib
make -C fpc/build gui-app
```

#### Windows
```bash
fpc -Fu../converter -Fu../common -Fu../json -Fu../platform -Fu../cli fpc/cli/ffmpeg_converter_windows.lpr
lazbuild fpc/gui/form.lpi
```

### Tests

Run Pascal test suite:
```bash
make -C fpc/build tests
```

Run a single/targeted Pascal test build:
```bash
make -C fpc/build tests TEST_PROGRAMS="test_cmd_builder"
```

Run one compiled test binary:
```bash
./fpc/test/test_cmd_builder
```

Integration scripts:
```bash
bash fpc/test/test_cli_args_matrix.sh
bash fpc/test/check_gui_cli_issues.sh
```

### Lint

No dedicated repo lint target is currently defined in top-level build/test tooling.

## High-level architecture

This repository contains two independent implementations that must stay behaviorally aligned:

1. `src/` (C/CMake): primary implementation across platforms  
   - Core converter engine: `src/converter/converter.c`  
   - Platform abstraction: `src/converter/converter_platform.h` with OS-specific implementations in `src/converter/platform/` plus runtime probing in `src/platform/*/runtime_probe.c`  
   - CLI: shared logic in `src/cli/cli_common.c` + per-platform codec/feature surfaces in `src/cli/platform/`  
   - GUI: Linux GTK4 in `src/gui/`, macOS native Cocoa in `src/gui_macos_native/`  
   - Post-process modules: `src/mux/` and `src/m4v/`

2. `fpc/` (Free Pascal, Linux/Windows): full parallel port  
   - Converter core: `fpc/converter/converter_core.pas`  
   - Tool/path resolver shared by CLI+GUI: `fpc/common/tool_paths.pas`  
   - CLI: `fpc/cli/ffmpeg_converter.lpr` and `fpc/cli/ffmpeg_converter_windows.lpr`  
   - GUI: Lazarus/LCL in `fpc/gui/`  
   - Tests: `fpc/test/`, orchestrated from `fpc/build/Makefile`

The converter path is intentionally split into:
- **analysis/preflight** (input/output checks, runtime capability detection),
- **command building** (ffmpeg command construction by codec/mode),
- **execution/progress/error reporting** (callbacks and per-file processing),
- **optional post-process workflows** (`mux`, `m4v`) that add external-tool stages.

## Key repository conventions

1. CLI semantics are consistent across C and Pascal:
   - Input files are positional (`file1 file2 ...`).
   - `-o/--output` is an output **directory**, never a filename.
   - If not set, output preflight creates and uses `$HOME/ffmpeg_converter`.

2. Runtime capability and tool detection drive feature availability:
   - Hardware codecs are probed at runtime; do not hardcode availability.
   - `mux` appears/works only when `mkvmerge` is resolvable.
   - `m4v` appears/works only when `MP4Box` is resolvable.

3. Tool resolution order is part of behavior:
   - Adjacent/bundled binaries near executable,
   - environment overrides (`FFMPEG_BIN`, `FFPROBE_BIN`, `MKVMERGE_BIN`, `MP4BOX_BIN` and platform aliases),
   - then `PATH`/platform-specific fallbacks.

4. Mode-specific invariants are strict and should be preserved in both implementations:
   - `mux` requires exactly one input and a readable `--video-track`.
   - `m4v` uses a multi-step pipeline (video copy + AAC + AC3 + MP4Box mux + optional chapters) and is validated as a dedicated mode, not a generic codec branch.

5. When changing behavior, keep C and Pascal parity unless a change is explicitly platform-specific:
   - mirror CLI options/validation,
   - mirror converter preflight and post-process rules,
   - keep feature gating and default output behavior aligned.
