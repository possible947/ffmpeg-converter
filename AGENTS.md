# AGENTS.md — Agent Development Guide

This file provides development guidelines for agents operating on this codebase.

## 1. Project Overview

ffmpeg_converter is a cross-platform media conversion tool with two implementations:

- **C/CMake** (`src/`) — original engine, CLI, GTK4 GUI (Linux), native Cocoa GUI (macOS)
- **Free Pascal** (`fpc/`) — full FPC port with CLI, shared library, Lazarus/LCL GUI

Both share the same conversion logic around external `ffmpeg`/`ffprobe`.

## 2. Build Commands

### 2.1 C/CMake

```bash
mkdir build && cd build
cmake ..
cmake --build . --target linux_cli
cmake --build . --target linux_gui
cmake --build . --target macos_cli
cmake --build . --target macos_gui_native
cmake --install . --prefix build/install
```

### 2.2 Free Pascal

```bash
make -C fpc/build cli        # CLI binary
make -C fpc/build lib        # Shared library
make -C fpc/build tests      # All tests
make -C fpc/build test_cmd_builder  # Single test (name, no ext)
lazbuild fpc/gui/form.lpi    # GUI binary
bash fpc/build/package_macos_app.sh  # Package .app (macOS)
```

Dependencies: C path (cmake ≥ 3.16, C compiler, jansson, ffmpeg/ffprobe, GTK4); FPC path (Free Pascal, Lazarus, ffmpeg/ffprobe, MP4Box).

## 3. C Code Style

**File organization**: standard libs → system → third-party → local headers.

**Brace style**: K&R variant (1TBS). Indentation: 4 spaces, no tabs. Line limit: 100 chars.

### Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Types | `snake_case_t` | `ConverterError` |
| Enums | `PREFIX_VALUE` | `ERR_OK` |
| Functions | `snake_case` | `converter_create` |
| Variables | `snake_case` | `exe_dir` |
| Macros | `SCREAMING_SNAKE` | `BUFFER_SIZE` |
| Structs | `CamelCase` | `Converter` |
| Callbacks | `on_action` | `on_file_begin` |

**Types**: Use fixed-width ints (`int32_t`, `uint64_t`) where size matters; `size_t` for sizes; `float` for progress.

**Error Handling**: Use `ConverterError` enum, return error codes, provide `converter_error_string()`. Check all `malloc`/`calloc` returns. Use `goto cleanup` pattern.

**Memory**: Use `calloc`; always `free` on error paths; set pointers to `NULL` after `free`.

**Strings**: Use `snprintf` (not `sprintf`); bounds-check with `sizeof`; use `strncpy` with explicit null termination.

**Platform guards**: `#if defined(__APPLE__)`, `#if defined(__linux__)`, `#ifdef _WIN32`.

## 4. Free Pascal Conventions

- Pascal case: `ConverterCreate`, `ConvertOptions`
- Unit names match filenames: `converter_core.pas` → `unit converter_core`
- Modes: `{$mode objfpc}`
- Interface/implementation sections required

## 5. Module Structure

**C path**: `src/converter/` (core), `src/cli/` (platform CLIs), `src/gui/` (Linux GTK4), `src/gui_macos_native/` (Cocoa), `src/platform/`, `src/audio/`, `src/video/`, `src/ffmpeg_cmd/`.

**FPC path**: `fpc/converter/` (engine + ABI), `fpc/common/`, `fpc/json/`, `fpc/cli/`, `fpc/gui/`, `fpc/test/`, `fpc/build/`.

## 6. Known Boundaries

- Windows C GUI not implemented
- C CLI lacks `--dry-run`
- Codecs: `copy`, `prores`, `prores_ks`, `h265_mi50`
- Audio normalization: `none`, `peak`, `peak2`, `loudnorm`, `loudnorm2`
- H.265 VAAPI: `h265_mi50` (requires VAAPI device)

## 7. References

- Overview: `README.md`
- Developer: `PROJECT_DESCRIPTION.md`
- Changelogs: `CHANGELOG.md`, `fpc/CHANGELOG.md`
- Install guides: `docs/install-*.md`