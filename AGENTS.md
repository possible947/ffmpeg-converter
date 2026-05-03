# AGENTS

## Scope

These instructions apply to the whole repository.

## Project Shape

- This repo contains two implementations that intentionally coexist:
  - `src/`: primary C/CMake implementation for Linux, macOS, and Windows CLI.
  - `fpc/`: Free Pascal implementation for Linux and Windows only.
- Do not assume every feature or platform path exists in both trees.
- macOS work should stay in the C path. Pascal macOS support was removed in v2.4.
- **No Windows GUI in C path** — only CLI; Pascal has Windows GUI.
- **Linux GTK4 GUI is C-only** — Pascal has separate Lazarus GUI.

## First Places To Read

- Project overview: [README.md](README.md)
- C architecture and targets: [src/README.md](src/README.md)
- Pascal architecture and build flow: [fpc/README.md](fpc/README.md)
- Platform setup details: [docs/install-linux.md](docs/install-linux.md), [docs/install-macos.md](docs/install-macos.md), [docs/install-windows.md](docs/install-windows.md)
- Product and feature detail: [docs/PROJECT_DESCRIPTION.md](docs/PROJECT_DESCRIPTION.md)

## Working Areas

- `src/converter/`, `src/ffmpeg_cmd/`, `src/progress/`: shared C conversion engine and ffmpeg command/progress handling.
- `src/gui/`: Linux GTK GUI (C-only).
- `src/gui_macos_native/`: macOS native Cocoa GUI (C-only).
- `src/m4v/`, `src/mux/`: Apple M4V and MKV mux workflows.
- `src/platform/`: platform-specific binaries and bundling scripts.
- `src/cli/`: CLI entry points per platform (main.c is unified, platform subdirs).
- `fpc/converter/`, `fpc/cli/`, `fpc/gui/`: Pascal engine, CLI, and GUI.
- `third_party/jansson/`: vendored dependency; avoid unrelated edits.
- `build/`, `fpc/bin/`: generated output; do not hand-edit build artifacts.

## Build And Validation — Exact Commands

### C/CMake Linux
```bash
cmake -S . -B build
cmake --build build --target linux_cli
cmake --build build --target linux_gui
# Binaries: build/bin/ffmpeg_converter, build/bin/ffmpeg_converter_gui
```

### C/CMake macOS
```bash
cmake -S . -B build
cmake --build build --target macos_cli
cmake --build build --target macos_gui_native
cmake --install build
# App: build/install/ffmpeg_converter_gui_macos.app
```
- Requires Xcode command-line tools. CMake must find MacPorts libraries (ffmpeg, jansson) at `/opt/local` typically.

### C/CMake Windows (MSVC)
```bash
# Preferred — uses PowerShell, auto-detects VS:
.\scripts\windows_build.ps1
# Or clean build:
.\scripts\windows_build.ps1 -Clean
```
- **Bundled binaries required BEFORE build**: `src/platform/windows/bin/` must contain `ffmpeg.exe`, `ffprobe.exe`, and all required DLLs. Build copies these next to `ffmpeg_converter.exe`.
- Manual: `cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64` then `cmake --build build-msvc --target windows_cli --config Release`.
- Output: `build-msvc/src/cli/Release/ffmpeg_converter.exe`.

### Free Pascal Linux
```bash
make -C fpc/build cli        # → fpc/bin/ffmpeg_converter
make -C fpc/build lib        # → fpc/converter/libconverter_pas.so
make -C fpc/build gui-app    # → fpc/gui/form.app (self-contained bundle)
make -C fpc/build tests      # → test binaries in fpc/test/
```

### Free Pascal Windows
```bash
# Compile CLI:
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json -Fu./fpc/cli -Fu./fpc/platform ./fpc/cli/ffmpeg_converter_windows.lpr -offmpeg_converter_windows.exe
# Or via Makefile (WSL/cross): see fpc/build/Makefile
lazbuild fpc/gui/form.lpi   # → fpc/gui/ffmpeg_converter_gui.exe
```

## Key Repo Conventions That Agents Must Know

- **CLI `-o/--output` is a directory, never a filename**. Default output is `$HOME/ffmpeg_converter` (created if missing).
- **CLI inputs are positional arguments**, not `--input` pairs.
- **`codec=mux` is a special workflow**: one source file + `--video-track <replacement>` → `mkvmerge` → `.mkv` output. `mkvmerge` must be present.
- **`m4v` is a GUI-only workflow** in CLI it is a separate codec choice that triggers Apple M4V creator (multi-step: video copy → AAC → AC3 → MP4Box). Requires `MP4Box` and accepts only `h264`/`hevc`/`prores` input video tracks.
- **Bundled tools matter**. Runtime expects `ffmpeg`/`ffprobe` staged in platform-specific `src/platform/*/bin/` or adjacent to the binary. Without bundled ffmpeg/ffprobe, many platform builds will fail or behave incorrectly.
- **Linux hardware codec availability is runtime-probed**, not compile-time. Agents should not modify source to hardcode VAAPI availability.
- **macOS Pascal is discontinued**. All macOS work uses C/Cocoa path.
- **Windows C GUI is not implemented**. Use C CLI (most complete) or Pascal GUI.
- **CMake feature switches**: `ENABLE_LINUX_GUI` (Linux), `ENABLE_MACOS_NATIVE_GUI` (macOS); both default ON on their platforms.

## Cross-Implementation Parity Notes

- C and Pascal implementations share conversion options/behavior. Changes intended to be cross-platform should be mirrored where both implementations exist (Linux/Windows). macOS features are C-only.
- Tool discovery order (both C and Pascal): 1) executable-adjacent, 2) env vars (`FFMPEG_BIN`, `FFPROBE_BIN`, `MKVMERGE_BIN`, `MP4BOX_BIN`), 3) `PATH`.
- C path uses `src/platform/<platform>/bin/` for bundled tools. Pascal GUI packaging also uses `src/platform/linux/bin/` for ffmpeg/ffprobe when creating AppImages.

## Easy-To-Miss Pitfalls

- **AV1 input decoding**: requires ffmpeg built with `--enable-libdav1d`. Default bundled ffmpeg on Linux includes it; some distro packages do not.
- **`mkvmerge` required for mux mode** on all platforms. If absent, mux codec is silently disabled or errors early.
- **`MP4Box` required for Apple M4V creator** on macOS (C) and Linux (GTK C and Pascal).
- **Windows MSVC build requires pre-staged binaries** in `src/platform/windows/bin/` (ffmpeg.exe, ffprobe.exe, DLLs) before building.
- **Linux AppImage packaging** requires `ffmpeg`/`ffprobe` in `src/platform/linux/bin/` before running `package_appimage`.
- **macOS native GUI .app bundling** will attempt to bundle MP4Box and dependent dylibs at build time (from platform/macos/bin).
- **Pascal GUI builds require `lazbuild` (Lazarus IDE)**. CLI builds only need `fpc`.
- **Do not edit vendored `third_party/jansson`** unless you intend to maintain the forked copy.
- **`prores_ks` (C) and Pascal may have different quality curves** — validate side-by-side when touching encoder parameters.

## Build Validation Shortcuts

- C Linux CLI test: `./build/bin/ffmpeg_converter --help`
- Pascal Linux CLI test: `./fpc/bin/ffmpeg_converter --help`
- Pascal unit tests: `make -C fpc/build tests` (or run individual `./fpc/test/test_*`)
- Confirm both implementations produce compatible results for a given conversion by comparing CLI help and codec lists.

## Testing Quirks

- Pascal integration tests may require `test.mp4` sample in repo or test data directory.
- Mux tests require `mkvmerge` present in `PATH` or bundled location.
- Apple M4V creator tests require `MP4Box` and compatible input files (h264/hevc/prores).
- Hardware codec tests (VAAPI, Vulkan) require compatible hardware/drivers and will be skipped silently if unavailable.

## Useful References

- User-facing behavior: [docs/user_manual.md](docs/user_manual.md)
- Cross-platform build notes: [INSTALL_BUILD_ALL_PLATFORMS.md](INSTALL_BUILD_ALL_PLATFORMS.md)
- Pascal converter library API: [fpc/converter/CONVERTER_LIBRARY_DETAIL.md](fpc/converter/CONVERTER_LIBRARY_DETAIL.md), [fpc/converter/API_MAP.md](fpc/converter/API_MAP.md)
- C architecture: [docs/PROJECT_DESCRIPTION.md](docs/PROJECT_DESCRIPTION.md)
- Install guides: [docs/install-linux.md](docs/install-linux.md), [docs/install-macos.md](docs/install-macos.md), [docs/install-windows.md](docs/install-windows.md)