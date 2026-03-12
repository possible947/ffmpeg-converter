# Converter Library Detailed Description

## 1. Scope
This document describes the converter library implementation and code components in both:
- Original C library (`src/converter/`)
- Free Pascal ported library (`fpc/converter/`)

## 2. Public API Surface (Concept)
The library exposes the following operations:
1. Create converter context.
2. Destroy converter context.
3. Set callback handlers.
4. Set conversion options.
5. Process a file list.
6. Stop processing.
7. Translate error code to text.

The Pascal shared library exports C ABI-compatible symbol names through `fpc/converter/converter_pas.lpr`.

## 3. Data Structures
- `ConvertOptions` / `TConvertOptions`: codec, profile, deblock, audio normalization mode, genre, 2-pass metrics, overwrite policy, output directory.
- `ConverterError` / `TConverterError`: normalized processing and system-level error states.
- `ConverterCallbacks` / `TConverterCallbacks`: event callbacks for file lifecycle, stage, progress, messages, errors, and queue completion.

## 4. C Library Components (`src/converter/`)
- `converter.h`: public API, types, callbacks, error enum.
- `converter.c`: core implementation.
- `LIBRARY API SPECIFICATION.md`: functional contract and threading expectations.

Main responsibilities inside `converter.c`:
- input file validation.
- output naming and overwrite behavior.
- peak 2-pass analysis.
- loudnorm 2-pass analysis and JSON extraction.
- ffmpeg command construction for codec/profile/deblock/audio modes.
- ffmpeg execution and progress parsing.
- callback dispatch per file and queue lifecycle.

## 5. Pascal Library Components (`fpc/converter/`)
- `converter_types.pas`: Pascal equivalents of API types and defaults.
- `converter_core.pas`: main processing engine and callback dispatch.
- `converter_cmd_builder.pas`: ffmpeg command generation logic.
- `converter_analysis.pas`: peak/loudnorm 2-pass analysis wrappers.
- `converter_runner.pas`: ffprobe/ffmpeg execution helper wrappers.
- `converter_api_c.pas`: API-facing wrapper delegating to core.
- `converter_pas.lpr`: shared library target exporting C ABI names.
- `converter_pas.h`: C header for linking with `libconverter_pas.so`.
- `API_MAP.md`: mapping between conceptual C API and Pascal modules.

## 6. Pascal Library Internal Responsibility Map
1. `converter_core.pas`
- lifecycle (`converter_create`, `converter_destroy`)
- state management (`Opts`, callbacks, stop flag)
- input/output checks and orchestration
- stage sequencing and callback invocation order

2. `converter_cmd_builder.pas`
- codec-specific ffmpeg arguments
- h265 VAAPI device and upload filter handling
- deblock filters
- audio normalization filter chains

3. `converter_analysis.pas`
- `volumedetect` parsing for peak 2-pass
- loudnorm analysis call and JSON metrics decode

4. `converter_runner.pas`
- duration probing via `ffprobe`
- encoding invocation via `ffmpeg`

## 7. Shared Library Artifact
- Output (Linux): `fpc/converter/libconverter_pas.so`
- Output (macOS): `fpc/converter/libconverter_pas.dylib`
- Output (Windows): `fpc/converter/converter_pas.dll`
- Exported symbols:
- `converter_create`
- `converter_destroy`
- `converter_set_callbacks`
- `converter_set_options`
- `converter_process_files`
- `converter_stop`
- `converter_error_string`

## 8. Build and Integration
Build from repository root:
```bash
make -C fpc/build lib
```

C/C++ integration points:
- include header: `fpc/converter/converter_pas.h`
- link flags: `-L fpc/converter -lconverter_pas`
- runtime loader path example: `LD_LIBRARY_PATH=fpc/converter ./your_app`

## 9. Current Non-GUI Status
- Converter library port is present and buildable as shared library.
- CLI path is implemented in Pascal and uses this library flow.
- GUI is intentionally excluded from the Pascal port scope.
