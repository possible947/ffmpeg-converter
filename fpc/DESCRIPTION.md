# FPC Port Description

This directory defines a Free Pascal architecture equivalent to the C converter library and CLI.

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

## Non-Goals In This Draft

- Full GUI rewrite
- Full CI/test automation
- Production-ready error telemetry

## Current Implementation Status

- Non-GUI port is implemented (`converter`, `cli`, `common`, `json`).
- CLI supports interactive menu and command-line options.
- Converter flow includes file validation, output overwrite checks, peak/loudnorm 2-pass analysis, command building, and encode execution.
- GUI from C project is intentionally excluded.
- Shared library entry point is available at `fpc/converter/converter_pas.lpr`.
- C ABI header for integration is `fpc/converter/converter_pas.h`.

## Verification Commands

```bash
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json -Fu./fpc/cli ./fpc/cli/ffmpeg_converter.lpr
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json ./fpc/test/test_cmd_builder.pas
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json ./fpc/test/test_path_parse.pas
```
