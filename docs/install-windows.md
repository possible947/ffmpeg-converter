# Windows Install and Build

This document covers Windows install/build for both project paths:
- C/CMake (`src/`) using MSVC (Visual Studio Build Tools)
- Free Pascal (`fpc/`)

## 1. C/CMake Path (MSVC only, recommended)

Install Visual Studio 2022 Build Tools (or Visual Studio 2022 with C++ workload).

### 1.1 Install dependencies
- Install Visual Studio Build Tools with MSVC (x64) and CMake tools.
- `ffmpeg`/`ffprobe` are **not** taken from system PATH for this build flow.

### 1.2 Prepare bundled ffmpeg payload
Before configuring/building, place Windows binaries in:

```text
src/platform/windows/bin/
  ffmpeg.exe
  ffprobe.exe
  *.dll (all runtime dependencies required by ffmpeg/ffprobe)
```

Rules:
- Keep all dependent DLLs in the same `bin` folder.
- Do not rely on system-installed ffmpeg for the C/MSVC target.
- `mkvmerge` and `MP4Box` may still be provided through system PATH at runtime.

### 1.3 Build target
From repository root in **x64 Native Tools Command Prompt for VS 2022**:
```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --target windows_cli --config Release
```

Optional debug build:

```powershell
cmake --build build-msvc --target windows_cli --config Debug
```

### 1.4 Output layout
Release output folder:

```text
build-msvc/src/cli/Release/
  ffmpeg_converter.exe
  ffmpeg.exe
  ffprobe.exe
  *.dll (copied from src/platform/windows/bin)
```

## 2. Free Pascal Path

### 2.1 Install dependencies
Install FPC. Install Lazarus too if GUI work is needed.

### 2.2 Build targets
From repository root:
```bash
make -C fpc/build cli
make -C fpc/build tests
```

Notes:
- `fpc/build/Makefile` includes a Windows shared-library output path (`fpc/converter/converter_pas.dll`).

## 3. GUI Notes
If Lazarus GUI build reports missing `Interfaces` or `Forms`, install full Lazarus/LCL widgetset packages for the active compiler/toolchain.

## 4. CLI Behavior Notes
- C CLI uses positional inputs: `ffmpeg_converter [options] file1 file2 ...`.
- `-o/--output` expects output directory path.
- If output directory is not set, default output directory is
	`$HOME/ffmpeg_converter` on Unix-like hosts.

## 5. CI/Release Notes
See `WINDOWS_BRANCH.md` for Windows release/tag workflow details in GitHub Actions.
