# Free Pascal Port — ffmpeg_converter (Version 2.6)

This folder contains the Free Pascal (FPC) implementation of the `ffmpeg-converter` project,
available for Windows release builds and maintenance.

## Platform Status (strategy)

- **Windows**: CLI + Lazarus/LCL GUI (native win32/win64 widgetset) with Vulkan GPU support.
  This is the native release and final-validation target.
- **Linux**: unsupported; use the C/CMake implementation.
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

The Windows release is built natively with FPC/Lazarus and PowerShell. FFmpeg is
prepared separately and staged in `src/platform/windows/bin/`; MSYS2 is not
required.

### CLI and GUI release

```powershell
.\scripts\windows_build_fpc.ps1 -Clean
```

The complete release is staged in `fpc/bin/` and contains the CLI, GUI,
`ffmpeg.exe`, `ffprobe.exe`, `mkvmerge.exe`, `MP4Box.exe`, their required
runtime files, and `presets.json`. The build stops if a required component is
missing or fails its startup check. Focused `-CLIOnly` and `-GUIOnly` builds
still stage and validate the complete runtime bundle.

### Shared library

```powershell
.\scripts\windows_build_fpc.ps1 -LibraryOnly
```

The library-only mode builds `fpc/converter/converter_pas.dll`; it does not
create an application release bundle. The release script compiles and runs the
focused bundle-resolution check in `fpc/test/test_windows_bundle_resolution.pas`;
there is no broader Pascal test suite.

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

- Pascal GUI/CLI is supported for Windows release builds only. Linux and macOS
  users should use the C/CMake implementations (`src/gui/` and
  `src/gui_macos_native/`).
- Pascal runtime resolves bundled tools beside the executable first; environment
  variables and PATH are development fallbacks.
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
2. Verify `--help`, `--codecs-list`, valid and invalid codec/preset pairs,
  and Vulkan auto/manual device selection.
3. Smoke-test each detected NVENC, AMF, QSV, and Vulkan codec on real hardware.
