# ffmpeg-converter

Two independent implementations coexist:
- `src/`: C/CMake — macOS native GUI, Linux GTK4 GUI, Windows CLI (MSVC)
- `fpc/`: Free Pascal — Linux & Windows GUI/CLI (macOS discontinued v2.4)

## Build Commands

### C/CMake (Linux)
```bash
cmake -S . -B build
cmake --build build --target linux_cli
cmake --build build --target linux_gui
# AppImage: cmake -S . -B build -DENABLE_APPIMAGE=ON && cmake --build build --target package_appimage
# Outputs: build/bin/ffmpeg_converter, build/bin/ffmpeg_converter_gui[-x86_64.AppImage]
```

### C/CMake (macOS)
```bash
cmake -S . -B build
cmake --build build --target macos_cli
cmake --build build --target macos_gui_native
cmake --install build  # → build/install/ffmpeg_converter_gui_macos.app
```

### C/CMake (Windows MSVC)
```powershell
# Requires ffmpeg.exe, ffprobe.exe, DLLs in src/platform/windows/bin/ first
.\scripts\windows_build.ps1              # incremental
.\scripts\windows_build.ps1 -Clean       # clean
.\scripts\windows_build.ps1 -BuildFPC    # C + Pascal CLI
.\scripts\windows_build.ps1 -BuildGUI    # C + Pascal GUI
.\scripts\windows_build.ps1 -FPCOnly     # Pascal CLI only
.\scripts\windows_build.ps1 -GUIOnly     # Pascal GUI only (lazbuild)
```

### Free Pascal (Linux)
```bash
make -C fpc/build cli        # → fpc/bin/ffmpeg_converter
make -C fpc/build lib        # → fpc/converter/libconverter_pas.so
make -C fpc/build gui-app    # → fpc/gui/form.app (self-contained)
make -C fpc/build appimage   # → fpc/bin/ffmpeg_converter_gui_fpc-x86_64.AppImage
make -C fpc/build tests      # → test binaries in fpc/test/
make -C fpc/build            # all: cli + lib + tests + copy-binaries
```

### Free Pascal (Windows)
```bash
# CLI: fpc -Fu../converter -Fu../common -Fu../json -Fu../cli -Fu../platform cli/ffmpeg_converter_windows.lpr
# GUI: lazbuild gui/form.lpi
# Or via CMake: cmake --build build-msvc --target fpc_converter_windows / fpc_gui_windows
```

## CLI Conventions (both implementations)

- Inputs are **positional arguments**, not `--input` pairs
- `-o/--output` sets an **output directory**, never a filename
- Default output: `$HOME/ffmpeg_converter` (created if missing)
- Hardware codecs (VAAPI/Vulkan/VideoToolbox/QSV) probed at runtime — never hardcode availability

## Special Workflows & Dependencies

| Codec/Mode | Requires | Notes |
|------------|----------|-------|
| `codec=mux` | `mkvmerge` | 1 source + `--video-track <file>` → `.mkv`. Silently disabled if missing |
| `codec=m4v` | `MP4Box` | Apple M4V: video copy → AAC → AC3 → MP4Box. Input: h264/hevc/prores only |
| AV1 decode | ffmpeg + `libdav1d` | Requires ffmpeg built with `--enable-libdav1d` |

**Tool discovery order:** 1) adjacent to binary, 2) env vars (`FFMPEG_BIN`, `FFPROBE_BIN`, `MKVMERGE_BIN`, `MP4BOX_BIN`), 3) `PATH`

## Testing

- **Pascal unit tests**: `make -C fpc/build tests` → binaries in `fpc/test/`
- Targeted runs: `make -C fpc/build tests TEST_PROGRAMS="test_cmd_builder test_cli_mode_matrix"`
- Integration scripts: `bash fpc/test/test_cli_args_matrix.sh`, `bash fpc/test/check_gui_cli_issues.sh`
- Full regression: `./fpc/test/run_all_regression_and_capture.sh` → report in `/tmp/ffc_regression_<ts>/`
- Hardware codec tests (VAAPI, Vulkan, VideoToolbox, QSV) skip silently if unavailable

## Platform Gotchas

- **Linux**: Hardware codecs probed at runtime (VAAPI); never hardcode availability. GTK4 dev pkg needed for GUI.
- **macOS**: Native Cocoa GUI only (C). App bundles `ffmpeg`, `ffprobe`, `MP4Box`. Pascal support dropped v2.4.
- **Windows MSVC**: Must pre-stage `ffmpeg.exe`, `ffprobe.exe`, all DLLs in `src/platform/windows/bin/` before build.
- **Windows Pascal**: GUI needs `lazbuild` (Lazarus); CLI only needs `fpc`. Vulkan ProRes encoder probed at runtime.
- **AppImage (Linux)**: Requires `appimagetool` in PATH.
- **Vendored**: `third_party/jansson/` — avoid unrelated edits.
- **mkvmerge/MP4Box**: Required for mux/M4V modes on all platforms; silently disabled if absent.

## Key Entry Points

| Implementation | CLI Entry | GUI Entry |
|----------------|-----------|-----------|
| C/Linux | `src/cli/cli_linux.c` | `src/gui/main.c` (GTK4) |
| C/macOS | `src/cli/cli_macos.c` | `src/gui_macos_native/main.m` (Cocoa) |
| C/Windows | `src/cli/cli_windows.c` | — (no C GUI) |
| Pascal/Linux | `fpc/cli/ffmpeg_converter.lpr` | `fpc/gui/main.lpr` (LCL) |
| Pascal/Windows | `fpc/cli/ffmpeg_converter_windows.lpr` | `fpc/gui/form.lpi` (LCL) |

## Core Converter Logic

- C: `src/converter/converter.c` — validation, 2-pass analysis, ffmpeg cmd building, progress parsing, output dir preflight
- Pascal: `fpc/converter/converter_pas.pas` — C ABI compatible, exports 8 symbols via `converter_pas.h`

## References

- Install guides: `docs/install-linux.md`, `docs/install-macos.md`, `docs/install-windows.md`
- Dependencies reference: `docs/DEPENDENCIES_ANALYSIS.md`
- C architecture: `docs/PROJECT_DESCRIPTION.md`
- Pascal converter lib: `fpc/converter/CONVERTER_LIBRARY_DETAIL.md`
- Changelogs: `CHANGELOG.md`, `fpc/CHANGELOG.md`