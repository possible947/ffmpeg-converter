# macOS Install and Build

This document covers macOS build and install for both project paths:
- C/CMake (`src/`) — native Cocoa/AppKit GUI + CLI
- Free Pascal (`fpc/`) — Lazarus/LCL GUI (`form.app` bundle) + CLI

---

## 1. C/CMake Path

### 1.1 Install dependencies

Homebrew:
```bash
brew install cmake jansson pkg-config
```

MacPorts:
```bash
sudo port install cmake jansson pkgconfig
```

> `jansson` provides the shared library needed for two-pass loudness
> normalization (loudnorm). The project uses vendored jansson **headers**
> from `third_party/jansson/`, but still links the system `libjansson`
> at build time.

> No GTK is required. The macOS GUI uses native Cocoa/AppKit.

### 1.2 Prepare bundled ffmpeg/ffprobe (required for GUI bundling)

The native macOS GUI bundles `ffmpeg` and `ffprobe` inside the `.app`.
Place static binaries in `src/platform/macos/bin/` before building:

```
ffmpeg-converter/
  src/platform/macos/bin/
    ffmpeg          ← place here
    ffprobe         ← place here
```

If these files are absent, CMake will emit a warning and the app will
fall back to the system PATH at runtime.

### 1.3 Build

From the repository root:

```bash
cmake -B build
cmake --build build --target macos_cli
cmake --build build --target macos_gui_native
```

To install the native GUI app bundle into `build/install/`:

```bash
cmake --install build
# → build/install/ffmpeg_converter_gui_macos.app
```

### 1.4 Run

```bash
# CLI
./build/src/cli/ffmpeg_converter --help

# Native GUI app bundle (self-contained)
open build/install/ffmpeg_converter_gui_macos.app
```

---

## 2. Free Pascal Path

### 2.1 Install dependencies

1. **FPC + Lazarus** — download from [lazarus-ide.org](https://www.lazarus-ide.org/)
   or install via MacPorts:
   ```bash
   sudo port install fpc lazarus
   ```

2. **MP4Box** (optional — required for Apple M4V creation only):
   ```bash
   sudo port install gpac
   ```
   The packaging script bundles `MP4Box` and all its non-system dylib
   dependencies from `/opt/local/bin/MP4Box` automatically. Without it,
   all other conversion modes work normally.

### 2.2 Prepare bundled ffmpeg/ffprobe

Same as section 1.2 — place `ffmpeg` and `ffprobe` in
`src/platform/macos/bin/`. The `package_macos_app.sh` script copies them into
`fpc/gui/form.app/Contents/Resources/bin/`.

### 2.3 Build

From the repository root:

```bash
# CLI (via Makefile)
make -C fpc/build cli
# → fpc/cli/ffmpeg_converter

# GUI binary (via lazbuild)
lazbuild fpc/gui/form.lpi
# → fpc/gui/ffmpeg_converter_gui

# Package into self-contained .app bundle
bash fpc/build/package_macos_app.sh
# → fpc/gui/form.app
```

> `lazbuild` must be in `PATH`. It is included with the Lazarus IDE
> installation (typically at `/usr/local/bin/lazbuild` or
> `/opt/local/bin/lazbuild` depending on install method).

### 2.4 Run

```bash
# CLI
./fpc/cli/ffmpeg_converter --help

# GUI app bundle (self-contained)
open fpc/gui/form.app
```

---

## 3. Runtime Notes

### Self-contained app bundles

Both GUI variants embed `ffmpeg` and `ffprobe` inside the `.app` and
use them automatically — no system `ffmpeg` installation required:

| App bundle | Bundled tools location |
|---|---|
| `build/install/ffmpeg_converter_gui_macos.app` | `Contents/Resources/bin/` |
| `fpc/gui/form.app` | `Contents/Resources/bin/` (+ `lib/` for MP4Box dylibs) |

### Apple Silicon

The bundled `ffmpeg`/`ffprobe` are Intel (x86_64) binaries. On Apple
Silicon they run transparently via **Rosetta 2**. No manual steps needed.

### CLI tool resolution

The CLI resolves `ffmpeg`/`ffprobe` from PATH by default. Override with
environment variables:

```bash
export FFMPEG=/path/to/ffmpeg
export FFPROBE=/path/to/ffprobe
./fpc/cli/ffmpeg_converter input.mov
```

For C CLI (`build/src/cli/ffmpeg_converter`):
- Inputs are positional (`ffmpeg_converter [options] file1 file2 ...`).
- `-o/--output` sets output directory.
- If `-o` is not specified, default output directory is `$HOME/ffmpeg_converter`
   and it is created automatically when missing.

---

## 4. Validation

Build and run all Pascal tests (CLI, command-builder, path-parse, mode matrix):

```bash
make -C fpc/build all
```

Full regression suite (Pascal + CLI parity checks, requires `ffmpeg` in PATH):

```bash
bash fpc/test/run_all_regression_and_capture.sh
```
