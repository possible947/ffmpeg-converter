# FPC Port Description

This directory contains the Free Pascal implementation of the ffmpeg-converter project, providing a fully functional CLI, shared library with C ABI, and Lazarus/LCL GUI.

## Platform Support

- **Linux**: Pascal CLI and GUI — full support, including VAAPI hardware codecs (`h264_vaapi`, `hevc_vaapi`).
- **Windows**: Pascal CLI and Lazarus GUI are supported. Windows-specific hardware codecs are runtime-probed (`h264_nvenc`, `hevc_nvenc`, `h264_amf`, `hevc_amf`, `h264_qsv`, `hevc_qsv`, `prores_ks_vulkan`) and shown only when available.
- **macOS**: Native C GUI (`cli_macos.c` / macOS app bundle) remains the primary macOS path. Pascal tooling and packaging scripts are also present for parity workflows.

## API Compatibility Scope

Exported API mirrors these C symbols:

- `converter_create`
- `converter_destroy`
- `converter_set_callbacks`
- `converter_set_options`
- `converter_process_files`
- `converter_stop`
- `converter_error_string`

## Design Notes

- Keep `TConvertOptions` and callback record layout stable (`packed record`) for ABI consistency.
- Use external process execution for `ffmpeg` and `ffprobe`, matching current C strategy.
- Keep parser and command-builder deterministic for testability.
- Isolate shell quoting/path normalization in `common/path_utils.pas`.

## Current Implementation Status

- **CLI**: full argument parsing (`-c`, `-p`, `-d`, `-a`, `-g`, `--overwrite`, `-o`) and interactive 9-step menu.
- **Converter core**: file validation, output overwrite checks, peak/loudnorm 2-pass analysis, command building, encode execution.
- **Shared library**: C ABI export via `fpc/converter/converter_pas.lpr` with header `fpc/converter/converter_pas.h`.
- **GUI**: Lazarus/LCL application (`fpc/gui/form.pas`) with threaded conversion, progress display, and Apple M4V creator workflow.
- **Apple M4V creator**: multi-step pipeline (video copy → AAC → AC3 → MP4Box mux → optional chapters) with direct and edit-before-mux modes.
- **Tests**: unit tests (`test_cmd_builder`, `test_path_parse`, `test_cli_mode_matrix`) and shell integration scripts (`test_cli_args_matrix.sh`, `check_gui_cli_issues.sh`).

## Verification Commands

```bash
# CLI binary
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json -Fu./fpc/cli ./fpc/cli/ffmpeg_converter.lpr

# Unit tests
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json ./fpc/test/test_cmd_builder.pas
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json ./fpc/test/test_path_parse.pas
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json -Fu./fpc/cli ./fpc/test/test_cli_mode_matrix.pas

# Integration tests (require ffmpeg in PATH)
bash fpc/test/test_cli_args_matrix.sh
bash fpc/test/check_gui_cli_issues.sh

# Windows build (CLI + GUI staging to fpc/bin)
powershell -ExecutionPolicy Bypass -File scripts/windows_build_fpc.ps1
```
