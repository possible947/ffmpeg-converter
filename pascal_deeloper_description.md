# Pascal Developer Description

## Purpose
This document describes the current state of the Free Pascal (`fpc/`) implementation of the ffmpeg-converter project. It is intended as a handoff and continuation reference for future development.

## Scope and Current Readiness
- The Pascal codebase mirrors the C converter architecture and keeps C API boundaries compatible where possible.
- CLI flows are implemented and validated with local parity-oriented scripts.
- GUI flow exists (Lazarus/LCL) and includes Apple M4V batch behavior from file lists.
- Shared library wrapper exists for external integration.
- Version baseline for this snapshot: `0.2.0` (from `fpc/build/fpmake.pp`).

## Top-Level Layout (`fpc/`)
- `build/`: build metadata and make helper (`fpmake.pp`, `Makefile`).
- `cli/`: command line program and argument/menu/callback/progress units.
- `common/`: reusable utility units for filesystem/path/process/time operations.
- `converter/`: conversion core, command building, analysis, runner, C-compatible API wrapper.
- `json/`: loudnorm JSON parser unit.
- `gui/`: Lazarus form app and resources.
- `test/`: parity and regression checks for CLI/converter behavior.

## Core Architecture
The Pascal implementation follows the same main split as the C project:
- options and callback types (`converter_types`)
- converter lifecycle and file processing loop (`converter_core`)
- analysis passes (`converter_analysis`)
- ffmpeg command assembly (`converter_cmd_builder`)
- process execution (`converter_runner`, `common/process_utils`)

This preserves behavioral mapping with `src/converter/converter.c` and `src/converter/converter.h`.

## Module Details

### `fpc/converter/converter_types.pas`
- Defines ABI-sensitive types:
  - `TConvertOptions`
  - `TConverterCallbacks`
  - `TConverterError`
- Contains defaults via `InitDefaultOptions`.
- Acts as the canonical schema for all converter pipeline units.

### `fpc/converter/converter_core.pas`
- Implements converter object lifecycle and `converter_process_files` loop.
- Validates files, computes output names, handles overwrite policy.
- Runs analysis stage for `peak_norm_2pass` and `loudness_norm_2pass`.
- Builds command and calls encode stage.
- Dispatches callback notifications (`on_file_begin/end`, `on_stage`, `on_error`, `on_complete`).

### `fpc/converter/converter_cmd_builder.pas`
- Produces ffmpeg command line from options.
- Handles codec branch behavior (`copy`, `prores*`, `h265_mi50`).
- Handles audio normalization filter chain variants.
- Includes explicit overwrite mode control:
  - `-y` when overwrite enabled
  - `-n` when overwrite disabled

### `fpc/converter/converter_analysis.pas`
- Implements two-pass helpers:
  - peak: `RunPeakTwoPass`
  - loudnorm: `RunLoudnormTwoPass`
- Uses `ffmpeg -nostdin` analysis commands and command output capture.
- Loudnorm parsing is JSON-based using last JSON block extraction plus `json/loudnorm_json.pas`.
- Writes debug dumps on analysis parse failures:
  - `/tmp/ffc_peak_fail.log`
  - `/tmp/ffc_loud_fail.log`

### `fpc/json/loudnorm_json.pas`
- Parses loudnorm JSON payload to typed metrics:
  - `input_i`, `input_tp`, `input_lra`, `input_thresh`, `target_offset`
- Uses invariant decimal parsing (dot separator) for reliability.

### `fpc/common/process_utils.pas`
- Central command execution/capture helper (`RunCommandCapture`).
- Critical reliability behavior: process is awaited (`poWaitOnExit`) before final read.
- This fixed truncation issues where only ffmpeg banner was captured and analysis metrics were missing.

### `fpc/converter/converter_runner.pas`
- Wraps duration probing and encode run helper.
- Uses `RunCommandCapture` for ffprobe and ffmpeg execution wrappers.

### `fpc/converter/converter_api_c.pas`, `converter_pas.h`, `converter_pas.lpr`
- C-facing wrapper surface for dynamic library integration.
- Enables consuming Pascal converter from C/C++ style code paths.

## CLI Part (`fpc/cli/`)
- Entry point: `ffmpeg_converter.lpr`.
- Supporting units:
  - `cli_args.pas`
  - `cli_menu.pas`
  - `cli_callbacks.pas`
  - `cli_progress.pas`
- Provides summary output, option mapping, callback-driven run flow.
- Works with parity scripts in `fpc/test/`.

## GUI Part (`fpc/gui/`)
- Lazarus project with form logic in `form.pas` and layout in `form.lfm`.
- Includes queue/list processing, start/stop flow, callback-to-UI bridging.
- Apple M4V path supports two list modes:
  - direct conversion from source list
  - main conversion first, then M4V from outputs, then intermediate cleanup
- GUI backup project files are retained under `fpc/gui/backup/`.

## Tests and Validation (`fpc/test/`)
Key files:
- `check_gui_cli_issues.sh`: integrated check script for overwrite, peak2, loudnorm2, Apple overwrite behavior.
- `test_cli_mode_matrix.pas`: mode matrix checks.
- `test_cli_args_matrix.sh`: argument parsing checks.
- `test_cmd_builder.pas`, `test_path_parse.pas`: targeted unit-like checks.
- `run_apple_m4v_test.pas`: Apple M4V execution harness.

## Known Good Behaviors in This Snapshot
- Main overwrite behavior confirmed in user validation runs.
- `peak2` analysis flow confirmed passing in user validation runs.
- `loudnorm2` analysis flow confirmed passing in user validation runs.
- Apple overwrite behavior confirmed in test harness.

## Known Constraints
- GUI requires Lazarus/LCL environment; plain FPC-only environment may not build GUI targets.
- ffmpeg/ffprobe/MP4Box availability is external dependency and must be present in runtime/build environment.
- Some debug dump behavior remains by design for fast diagnosis when analysis parsing fails.

## Build and Run Notes
- Build commands are driven from `fpc/build/Makefile`.
- Version metadata currently sourced from `fpc/build/fpmake.pp`.
- Runtime and tests are typically executed from repository root with explicit output directories under `/tmp`.

## Repository Hygiene for Pascal Tree
- Added `fpc/.gitignore` to exclude generated FPC/Lazarus artifacts:
  - `*.o`, `*.ppu`, `*.or`, `*.so`, `*.compiled`, `*.res`
  - generated binaries and temporary media outputs
- Generated files should not be committed.

## Recommended Next Development Steps
1. Complete C-to-Pascal parity audit for remaining non-audio command flags and edge-case branches.
2. Add deterministic parsing/behavior tests around known fragile ffmpeg output variations.
3. Expand CLI argument parity tests for invalid/ambiguous combinations.
4. Extract GUI orchestration edge-cases into repeatable integration tests where possible.
5. Add release version source-of-truth file if multi-place versioning grows.
