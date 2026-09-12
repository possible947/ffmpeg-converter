# Free Pascal Port — ffmpeg_converter (Version 2.6)

This folder contains the Free Pascal (FPC) implementation of the `ffmpeg-converter` project,
available for Windows release builds and Linux development/debug builds.

## Platform Status (strategy)

- **Windows**: CLI + Lazarus/LCL GUI (native win32/win64 widgetset) with Vulkan GPU support.
  This is the native release and final-validation target.
- **Linux**: CLI/GUI development and debug target. The Pascal implementation is
  functionally usable and is used to debug shared behavior before Windows testing,
  but the Linux GUI still has known interface issues and is not a release package.
- **macOS**: Pascal support is discontinued; use the C/CMake implementation.

## Features

- C API parity — exports all 8 converter symbols with ABI-compatible types
- CLI with argument parsing and interactive multi-step menu
- Lazarus/LCL GUI with threaded conversion and progress display
- GUI parity updates: audio output selector, mux video-track workflow, platform-aware codec list and widget gating
- Windows GUI polish: no console popups for subprocesses (`poNoConsole`), Vulkan encoder/device probing, and Vulkan device selector in the action row
- Apple M4V creator with multi-step mux pipeline
  (video copy + AAC + AC3 + MP4Box, optional chapter transfer via `ffmpeg -map_chapters`)
- 2-pass peak and loudnorm (EBU R128) audio analysis
- Platform-aware codecs: Linux (`h264_vaapi`, `hevc_vaapi`), Windows (`*_nvenc`, `*_amf`,
  `*_qsv`, `prores_ks_vulkan`), both via runtime probing
- Audio output modes: `pcm`, `fdk_aac_320`, `fdk_aac_320_ac3_640`
- Mux mode parity: `-c mux --video-track <file>` with a preset-selected final
  container (`mkv` mkvmerge, `mov` ffmpeg remux, `m4v` Apple M4V pipeline)

## Folder Layout

- `converter/`: core engine, C ABI export, command builder, analysis, runner, Apple M4V creator
- `common/`: reusable file, process, path, and time helpers
- `json/`: loudnorm JSON parsing (using `fpjson`/`jsonparser`)
- `cli/`: CLI binary — argument parsing, interactive menu, progress display
- `gui/`: Lazarus/LCL GUI application with threaded workers
- `test/`: unit tests and integration test scripts

## Build

The supported Windows release build uses the Makefile from a Windows environment
(`make -C fpc/build ...`). Linux development GUI builds can be performed directly
with Lazarus, for example:

```bash
lazbuild --ws=gtk3 fpc/gui/form.lpi
lazbuild --ws=qt6 fpc/gui/form.lpi
```

These Linux builds are development/debug builds and do not replace native Windows
release validation.

LCL availability is distribution-dependent. The tested Ubuntu Linux 24.04.4
environment, using Lazarus from external sources, did not provide the required
LCL units. Fedora Linux 44 provides the LCL packages and supports the GTK3 and
Qt6 builds shown above.

### CLI binary

```bash
make -C fpc/build cli        # → fpc/bin/ffmpeg_converter.exe
```

The direct `cli` and `gui` targets stage `presets.json` next to their binaries.

### Shared library

```bash
make -C fpc/build lib        # → fpc/converter/converter_pas.dll
```

Direct invocation:

```bash
fpc -Cg -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json ./fpc/converter/converter_pas.lpr
```

> Note: `-Cg` enables position-independent code (PIC), required for shared library linking on x86_64.

### GUI (requires Lazarus IDE or lazbuild)

```bash
make -C fpc/build gui
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

- CLI binary: `fpc/bin/ffmpeg_converter.exe`
- Shared library: `fpc/converter/converter_pas.dll`
- GUI binary: `fpc/bin/ffmpeg_converter_gui.exe`

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

## Notes

- Pascal GUI/CLI is release-supported on Windows. Linux Pascal remains a functional
  development/debug implementation; production Linux users should use the C/CMake
  implementation (`src/gui/`). macOS users should use `src/gui_macos_native`.
- Pascal runtime resolves tools for GUI/CLI launches using a unified resolver
  (`ffmpeg`, `ffprobe`, `MP4Box`, `mkvmerge`): executable-adjacent dir → env vars → PATH.
- `converter_set_options` validates Windows hardware codec capabilities at runtime.
- Windows GUI runtime-probes NVENC/AMF/QSV/Vulkan and conditionally exposes matching codecs
  in the codec combobox.
- `prores_ks_vulkan` uses dedicated Vulkan probe logic and supports explicit or auto device
  index selection.
- CLI `-o/--output` creates missing output directories before conversion and fails early on
  invalid/unwritable targets.
- The interactive menu builds its codec list dynamically from the Windows runtime
  probe — only codecs that are actually available are offered.
- `--codecs-list`, CLI parsing, and the interactive preset step read the same
  `presets.json` database; `--profile` remains a deprecated alias for `--preset`.

## Windows Release Verification Required

Run these checks on a real Windows host before release:

1. Run `scripts/windows_build_fpc.ps1` for CLI and GUI, then confirm
  `fpc/bin/presets.json` is staged beside the executables.
2. Run `scripts/windows_build.ps1 -BuildFPC -BuildGUI`, then confirm
  `build-msvc/fpc/presets.json` is present for the legacy combined path.
3. Verify `--help`, `--codecs-list`, valid and invalid codec/preset pairs,
  and Vulkan auto/manual device selection.
4. Smoke-test each detected NVENC, AMF, QSV, and Vulkan codec on real hardware.
