# Free Pascal Port — ffmpeg_converter (Version 2.6)

This folder contains the Free Pascal (FPC) implementation of the `ffmpeg-converter` project,
available for **Linux and Windows only**. macOS support was discontinued in version 2.4.

## Platform Status (strategy)

- **Linux**: CLI + shared library are first-class citizens. The **primary Linux GUI is the
  C/GTK4 implementation** (`src/gui/`); the Pascal/LCL GUI on Linux is legacy and kept only
  for feature parity testing — it is built with the GTK3 widgetset and has no GTK4 support.
- **Windows**: CLI + Lazarus/LCL GUI (native win32/win64 widgetset) with Vulkan GPU support.
  This is the **only GUI on Windows**.
- **macOS**: Discontinued in v2.4 (use C/CMake native Cocoa GUI instead).

> Conclusion of the Linux audit (P4): the Pascal GUI is **not** developed further for Linux —
> LCL has no GTK4 widgetset, so the C/GTK4 GUI is the strategic Linux GUI. Pascal remains
> valuable on Linux as the CLI and the shared `libconverter_pas.so` library.

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
- Mux mode parity: `-c mux --video-track <file>` with mkvmerge post-process pipeline

## Folder Layout

- `converter/`: core engine, C ABI export, command builder, analysis, runner, Apple M4V creator
- `common/`: reusable file, process, path, and time helpers
- `json/`: loudnorm JSON parsing (using `fpjson`/`jsonparser`)
- `cli/`: CLI binary — argument parsing, interactive menu, progress display
- `gui/`: Lazarus/LCL GUI application with threaded workers
- `test/`: unit tests and integration test scripts

## Build

Prefer the Makefile (`make -C fpc/build ...`); it handles platform-specific flags
(Windows path mangling via `cygpath`, Linux `--ws=gtk3` widgetset, macOS hard-block).

### CLI binary

```bash
make -C fpc/build cli        # → fpc/bin/ffmpeg_converter
```

Direct invocation:

```bash
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json -Fu./fpc/cli ./fpc/cli/ffmpeg_converter.lpr
```

### Shared library

```bash
make -C fpc/build lib        # → fpc/converter/libconverter_pas.so (Linux)
```

Direct invocation:

```bash
fpc -Cg -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json ./fpc/converter/converter_pas.lpr
```

> Note: `-Cg` enables position-independent code (PIC), required for shared library linking on x86_64.

### GUI (requires Lazarus IDE or lazbuild)

Linux — **Qt6 widgetset** is the default and recommended for GNOME + Wayland
(LCL GTK3 is still alpha in Lazarus):

```bash
make -C fpc/build gui        # Qt6 (default) → fpc/bin/ffmpeg_converter_gui
# GTK3 fallback:
make -C fpc/build gui GUI_WS=gtk3
```

> **Qt6 requires `libQt6Pas.so.6`** (the Pascal bindings for Qt6). Ubuntu/Debian
> apt ships **no** `libqt6pas` package, so build it from the bindings that ship
> with Lazarus:
> ```bash
> cd /usr/share/lazarus/*/lcl/interfaces/qt6/cbindings
> qmake6 && make && sudo make install && sudo ldconfig
> ```
> (Dev packages `qt6-base-dev` + `qt6-base-dev-tools` must be installed first:
> `sudo apt install qt6-base-dev qt6-base-dev-tools`.)
>
> **Lazarus source**: Ubuntu/Debian apt ships `lcl-gtk2`/`lcl-qt5` only — no
> `lcl-gtk3` and no Qt6 bindings. Install Lazarus from lazarus-ide.org; it
> includes both the GTK3 and Qt6 LCL interfaces.
>
> If the build fails, the Makefile prints the real compiler errors plus a
> per-cause hint (GTK3 dev libs, missing Qt6Pas, missing widgetset). Full log:
> `fpc/build/.units/gui-build.log`.

Windows (native widgetset):

```bash
make -C fpc/build gui
```

AppImage packaging (Linux):

```bash
make -C fpc/build appimage   # (gui-app is an alias of appimage)
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

- CLI binary: `fpc/bin/ffmpeg_converter`
- Shared library (Linux): `fpc/converter/libconverter_pas.so`
- Shared library (Windows): `fpc/converter/converter_pas.dll`
- GUI binary: `fpc/bin/ffmpeg_converter_gui`

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

- Pascal GUI/CLI supports Linux and Windows. macOS users should use the native C GUI
  (`src/gui_macos_native`).
- Pascal runtime resolves tools for GUI/CLI launches using a unified resolver
  (`ffmpeg`, `ffprobe`, `MP4Box`, `mkvmerge`): executable-adjacent dir → env vars → PATH.
- `converter_set_options` validates platform capabilities for hardware codecs
  (VAAPI rejected on non-Linux, Linux probes encoder availability).
- Windows GUI runtime-probes NVENC/AMF/QSV/Vulkan and conditionally exposes matching codecs
  in the codec combobox.
- `prores_ks_vulkan` uses dedicated Vulkan probe logic and supports explicit or auto device
  index selection.
- CLI `-o/--output` creates missing output directories before conversion and fails early on
  invalid/unwritable targets.
- The interactive menu (Linux/Windows) builds its codec list dynamically from the runtime
  probe — only codecs that are actually available are offered.
