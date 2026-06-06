# Free Pascal Port — ffmpeg_converter (Version 2.4)

This folder contains the Free Pascal (FPC) implementation of the `ffmpeg-converter` project,
available for **Linux and Windows only**. macOS support was discontinued in version 2.4.

## Platform Status

- **Linux**: CLI + Lazarus/LCL GUI (feature-matched with C implementation)
- **Windows**: CLI + Lazarus/LCL GUI with Vulkan GPU support
- **macOS**: Discontinued in v2.4 (use C/CMake native Cocoa GUI instead)

## Features

- C API parity — exports all 8 converter symbols with ABI-compatible types
- CLI with argument parsing and interactive multi-step menu
- Lazarus/LCL GUI with threaded conversion and progress display
- GUI parity updates: audio output selector, mux video-track workflow, platform-aware codec list and widget gating
- Windows GUI polish: no console popups for subprocesses (`poNoConsole`), Vulkan encoder/device probing, and Vulkan device selector in the action row
- Apple M4V creator with multi-step mux pipeline
  (video copy + AAC + AC3 + MP4Box, optional chapter transfer via `ffmpeg -map_chapters`)
- 2-pass peak and loudnorm (EBU R128) audio analysis
- Platform-aware codecs: Linux (`h264_vaapi`, `hevc_vaapi`)
- Audio output modes: `pcm`, `fdk_aac_q5`, `fdk_aac_q5_ac3_640`
- Mux mode parity: `-c mux --video-track <file>` with mkvmerge post-process pipeline

## Folder Layout

- `converter/`: core engine, C ABI export, command builder, analysis, runner, Apple M4V creator
- `common/`: reusable file, process, path, and time helpers
- `json/`: loudnorm JSON parsing (using `fpjson`/`jsonparser`)
- `cli/`: CLI binary — argument parsing, interactive menu, progress display
- `gui/`: Lazarus/LCL GUI application with threaded workers
- `test/`: unit tests and integration test scripts

## Build

### CLI binary

```bash
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json -Fu./fpc/cli ./fpc/cli/ffmpeg_converter.lpr
```

### Shared library

```bash
fpc -Cg -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json ./fpc/converter/converter_pas.lpr
```

> Note: `-Cg` enables position-independent code (PIC), required for shared library linking on x86_64.

### GUI (requires Lazarus IDE or lazbuild)

```bash
make -C fpc/build gui-app
```

Direct lazbuild invocation is also supported:

```bash
lazbuild -B ./fpc/gui/form.lpi
bash ./fpc/build/package_macos_app.sh ./fpc/gui/ffmpeg_converter_gui
```

### Tests

```bash
make -C fpc/build tests
```

Targeted parity checks:

```bash
make -C fpc/build tests TEST_PROGRAMS="test_cmd_builder test_cli_mode_matrix test_unified_tool_resolver"
./fpc/test/test_cmd_builder
./fpc/test/test_cli_mode_matrix
./fpc/test/test_unified_tool_resolver
```

Shell-based integration tests:

```bash
bash fpc/test/test_cli_args_matrix.sh
bash fpc/test/check_gui_cli_issues.sh
```

Unified regression run (build + unit + integration + Pascal/C parity):

```bash
./fpc/test/run_all_regression_and_capture.sh
```

The script writes a timestamped report under `/tmp/ffc_regression_<timestamp>/`
with `summary.txt`, `status.tsv`, logs, and parity artifacts.

### Generated artifacts

- CLI binary: `fpc/cli/ffmpeg_converter`
- Shared library (Linux): `fpc/converter/libconverter_pas.so`
- Shared library (macOS): `fpc/converter/libconverter_pas.dylib`
- Shared library (Windows): `fpc/converter/converter_pas.dll`
- GUI binary: `fpc/gui/ffmpeg_converter_gui`

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

## Documentation

- Converter library API detail: `fpc/converter/CONVERTER_LIBRARY_DETAIL.md`
- Code review report: `fpc/REVIEW_REPORT.md`
- Optimization audit: `fpc/OPTIMIZATION_AUDIT.md`
- Cross-platform install guides: `docs/install-linux.md`, `docs/install-macos.md`, `docs/install-windows.md`

## macOS Notes

- Pascal CLI/GUI supports Linux and Windows. macOS users should primarily use the native C GUI (`cli_macos.c` / the macOS app bundle).
- Pascal runtime resolves tools for GUI/CLI launches using a unified resolver (`ffmpeg`, `ffprobe`, `MP4Box`, `mkvmerge`), including `.app` bundled tools in `Contents/Resources/bin`.
- `converter_set_options` now validates platform capabilities for hardware codecs (VAAPI rejected on non-Linux, Linux probes encoder availability).
- Windows GUI runtime-probes NVENC/AMF/QSV/Vulkan and conditionally exposes matching codecs in the codec combobox.
- `prores_ks_vulkan` now uses dedicated Vulkan probe logic and supports explicit or auto device index selection.
- CLI `-o/--output` creates missing output directories before conversion and fails early on invalid/unwritable targets.
