# Pascal Branch — Full Code Review Report

**Date:** 2026-03-12
**Scope:** All Pascal source files under `fpc/`
**Reviewer:** Automated deep review (architecture, logic, build system, tests)

---

## 1. Project Summary

The `fpc/` directory contains a **~4,600-line Free Pascal port** of the C `ffmpeg_converter` project.

**Deliverables:**
- CLI application (`fpc/cli/ffmpeg_converter.lpr`)
- Shared library with C ABI (`fpc/converter/converter_pas.lpr` → `libconverter_pas.so`)
- Lazarus/LCL GUI application (`fpc/gui/form.pas`, `main.lpr`)
- Apple M4V muxing module (`fpc/converter/apple_m4v_creator.pas`)
- Unit and integration tests (`fpc/test/`)

---

## 2. Architecture & Module Map

| Module | Files | Lines | Purpose |
|--------|-------|-------|---------|
| **converter** | `converter_core.pas`, `converter_types.pas`, `converter_api_c.pas`, `converter_cmd_builder.pas`, `converter_analysis.pas`, `converter_runner.pas`, `converter_pas.lpr`, `converter_pas.h`, `apple_m4v_creator.pas` | ~1,109 | Core engine, C ABI export, command building, analysis, encoding runner, Apple M4V workflow |
| **cli** | `ffmpeg_converter.lpr`, `cli_args.pas`, `cli_menu.pas`, `cli_callbacks.pas`, `cli_progress.pas` | ~1,184 | CLI binary: argument parsing, interactive menu, progress display |
| **common** | `fs_utils.pas`, `path_utils.pas`, `process_utils.pas`, `time_utils.pas` | ~183 | Shared utilities: file checks, shell quoting, process execution, time formatting |
| **json** | `loudnorm_json.pas` | ~71 | Loudnorm JSON metrics parser (using `fpjson`/`jsonparser`) |
| **gui** | `form.pas`, `form.lfm`, `form.lpi`, `main.lpr` | ~1,121 | Lazarus/LCL GUI with threaded conversion and Apple M4V creator |
| **test** | `test_cmd_builder.pas`, `test_path_parse.pas`, `test_cli_mode_matrix.pas`, `run_apple_m4v_test.pas`, shell scripts | ~178 + scripts | Unit and integration tests |

---

## 3. C API Parity

The Pascal port exports **all 7 C API symbols** through `converter_pas.lpr`:

- `converter_create`
- `converter_destroy`
- `converter_set_callbacks`
- `converter_set_options`
- `converter_process_files`
- `converter_stop`
- `converter_error_string`

`TConvertOptions` and `TConverterCallbacks` are `packed record` types, field-for-field matching the C `ConvertOptions` and `ConverterCallbacks` structs. The C header `fpc/converter/converter_pas.h` mirrors this exactly.

**ABI compatibility is correct.**

The `converter_api_c.pas` adapter layer is a clean delegate-through to `converter_core.pas` — reasonable separation of the C ABI surface from implementation.

---

## 4. Logic Implementation — Detailed Findings

### 4.1 Converter Core (`converter_core.pas`)

**What works correctly:**
- File processing loop with proper `on_file_begin` / `on_file_end` lifecycle per file.
- Input validation: `fpStat` + `FPS_ISREG` + `fpAccess` — matches C logic.
- Output existence check with overwrite flag.
- 2-pass peak and loudnorm analysis dispatch based on `audio_norm` string.
- Automatic `use_aac_for_h265` flag set for `h265_mi50` codec.
- Genre target application before loudnorm analysis.
- Stop flag checked between files.

**Issues:**

| ID | Severity | Description |
|----|----------|-------------|
| CORE-1 | Minor | **StopFlag is not thread-safe.** `StopFlag` is a plain `LongInt` read/written without any atomic operation or memory barrier. The GUI calls `converter_stop` from the main thread while the worker thread reads the flag. On x86 this works in practice for a simple int, but it's technically a data race. Consistent with C behavior. |
| CORE-2 | Minor | **`RunMenu` zeroes the options record with `FillChar`.** `cli_menu.pas` line 227 zeroes the entire `TConvertOptions` record, discarding any defaults set by `InitDefaultOptions` in the caller. The menu re-populates all fields at step 9, so no data is currently lost — but if a new field with a non-zero default is added, it will be silently zeroed. |
| CORE-3 | Minor | **`converter_process_files` always returns `ERR_OK` for mixed batches.** Individual file failures are reported via `on_file_end` callbacks, but the function return is `ERR_OK` unless stopped early. The CLI checks return value for exit code, so partial failures yield exit code 0. Matches C behavior. |

### 4.2 Command Builder (`converter_cmd_builder.pas`)

**What works correctly:**
- All codec branches (`copy`, `prores`, `prores_ks`, `h265_mi50`) produce correct ffmpeg arguments.
- Overwrite flag emits `-y` or `-n`.
- VAAPI device path supports `VAAPI_DEVICE` environment variable override.
- All 5 audio normalization modes produce correct `-af` filter chains.
- Shell quoting via `QuoteForShell` applied to input/output paths and VAAPI device.
- Loudnorm 2-pass includes all measured metrics with `linear=true`.

**Issues:**

| ID | Severity | Description |
|----|----------|-------------|
| CMD-1 | Info | **Deblock is silently ignored for h265_mi50.** When `h265_mi50` is selected, `-vf "format=nv12,hwupload"` always takes precedence. No warning is emitted if the user also selects deblock. Matches C behavior — by design. |

### 4.3 Analysis (`converter_analysis.pas`)

**What works correctly:**
- `RunPeakTwoPass`: executes `ffmpeg -nostdin -vn -i <file> -af volumedetect -f null -` and parses `max_volume:` from stderr.
- `RunLoudnormTwoPass`: executes loudnorm analysis and extracts the last JSON block using `RPos('{')` / `RPos('}')` — robust for ffmpeg output.
- Both methods dump failure output to `/tmp/ffc_*.log` for debugging.
- `ExtractNumberAfterToken` finds the last occurrence of a token (handles repeated output lines correctly).

**Issues:**

| ID | Severity | Description |
|----|----------|-------------|
| ANA-1 | Functional | **`RunPeakTwoPass` does not check `R.ExitCode`.** Only inspects output text for `max_volume:`. If ffmpeg exits non-zero but still emits partial output with `max_volume:`, analysis could produce wrong data. `RunLoudnormTwoPass` correctly checks exit code first — this is inconsistent. |
| ANA-2 | Info | **Hardcoded `/tmp/` diagnostic paths.** Debug dump files (`/tmp/ffc_peak_fail.log`, `/tmp/ffc_loud_fail.log`) are Linux-specific. Acceptable for current target but limits portability. |

### 4.4 Runner (`converter_runner.pas`)

**Issues:**

| ID | Severity | Description |
|----|----------|-------------|
| RUN-1 | Functional | **`ProbeDuration` uses raw string quoting instead of `QuoteForShell`.** Builds the ffprobe command with `'"' + InputFile + '"'` — breaks if filename contains a literal double-quote character. |
| RUN-2 | Info | **`ProbeDuration` is dead code.** Defined but never called anywhere in the codebase. |
| RUN-3 | Functional | **Progress reporting during encoding is non-functional.** `RunEncode` appends `-progress pipe:1` to the command but then captures all output with `RunCommandCapture` (which blocks until completion). The output is never parsed. The `on_progress_encode` callback only receives a synthetic 100% event from `converter_core.pas`. Real-time progress bar is effectively broken — it jumps from 0% to 100%. |

### 4.5 Process Execution (`process_utils.pas`)

**What works correctly:**
- Uses `TProcess` with `/bin/sh -c` for command execution.
- `poUsePipes` + `poStderrToOutput` + `poWaitOnExit` — captures combined stdout+stderr and waits for completion.

**Issues:**

| ID | Severity | Description |
|----|----------|-------------|
| PROC-1 | Critical | **Potential deadlock with large output.** `poUsePipes` + `poWaitOnExit` can deadlock if the subprocess produces more output than the OS pipe buffer (~64KB on Linux). `Execute` blocks until the child exits, but the child blocks waiting for the pipe to be read. For short analysis commands this is fine. For long encodes with verbose output, this can hang indefinitely. Known FPC `TProcess` pitfall. |

### 4.6 Shell Quoting (`path_utils.pas`)

**Issues:**

| ID | Severity | Description |
|----|----------|-------------|
| PATH-1 | Critical | **`QuoteForShell` only escapes double quotes.** Implementation: `'"' + StringReplace(S, '"', '\"', [rfReplaceAll]) + '"'`. Within double quotes in bash, the characters `$`, `` ` ``, `\`, and `!` also have special meaning. A filename like `test$HOME.mov` or `` test`cmd`.mov `` would be misinterpreted. This is a **shell injection / correctness risk**. |

### 4.7 JSON Parsing (`loudnorm_json.pas`)

**What works correctly:**
- Uses FPC's built-in `fpjson`/`jsonparser` (no external dependency, unlike C which needs jansson).
- Proper invariant decimal separator handling with `TFormatSettings`.
- Graceful error handling with `try..except` block.
- Extracts all 5 required loudnorm metrics.

**Issues:**

| ID | Severity | Description |
|----|----------|-------------|
| JSON-1 | Info | **`J.Free` pattern is non-idiomatic.** `J.Free` is placed outside the `try..except` block. This works (nil.Free is safe in FPC) but a `try..finally` for cleanup would be more conventional. |

### 4.8 Apple M4V Creator (`apple_m4v_creator.pas`)

**What works correctly:**
- Multi-step pipeline: video copy → AAC encode → AC3 encode → MP4Box mux → optional chapter import.
- Proper temp directory creation with cleanup via `try..finally`.
- FPS probing with fallback from `avg_frame_rate` to `r_frame_rate`.
- ffmpeg/ffprobe version discovery (tries ffmpeg8/7/6/ffmpeg).
- Chapter JSON parsing and OGG chapter text format conversion.
- `QuoteForShell` used consistently in all command construction.

**Issues:**

| ID | Severity | Description |
|----|----------|-------------|
| M4V-1 | Info | **`CleanupWorkDir` uses `rm -rf` via shell.** Calls `RunCommandCapture('/bin/rm -rf ' + QuoteForShell(WorkDir))`. Combined with PATH-1 quoting weakness, this is theoretically risky. In practice, `WorkDir` is generated internally so the risk is negligible. |
| M4V-2 | Info | **`Randomize` called on every `CreateWorkDir` invocation.** Re-seeds the RNG each time. Harmless for temp dir creation but not best practice. |

### 4.9 GUI (`form.pas`)

**What works correctly:**
- Uses `TThread` with `FreeOnTerminate := True` for background conversion.
- Thread-safe UI updates via `Application.QueueAsyncCall` with heap-allocated data records.
- Proper mutex pattern: prevents starting conversion while Apple M4V is running and vice-versa.
- `TAppleM4VThread` supports two workflows: direct mode and edit-before-mux mode.
- All 8 converter callbacks properly marshalled to main thread.
- UI state management (enabled/disabled controls) is consistent.
- Lazarus project file (`form.lpi`) correctly references unit search paths.

**Issues:**

| ID | Severity | Description |
|----|----------|-------------|
| GUI-1 | Minor | **`GMainForm` global can be used after form destruction.** Set in `FormCreate`, never cleared. If the form is destroyed while a thread is still finishing, callbacks could reference a freed object. `FreeOnTerminate` means the thread can outlive the form briefly. |
| GUI-2 | Info | **`TConverterThread.Execute` creates local `PAnsiChar` array from `FFiles`.** Safe because `FFiles` is a thread-private field never modified externally, but fragile if code evolves. |

---

## 5. CLI Implementation Review

**What works correctly:**
- Full argument parsing with validation for all options (`-c`, `-p`, `-d`, `-a`, `-g`, `--overwrite`, `-o`).
- Interactive multi-step menu with back/cancel navigation (9 steps).
- File verification with compaction of valid file entries.
- Input path parsing handles escaped characters, single/double quotes.
- Summary output with ANSI escape codes for screen clearing.
- Memory management: `StrNew`/`StrDispose` used correctly for file paths.
- `ClearAllocated` properly frees on menu cancel.

**No functional issues found in CLI argument parsing or menu logic.**

---

## 6. Test Coverage Assessment

| Test File | Coverage | Quality |
|-----------|----------|---------|
| `test_cmd_builder.pas` | Command prefix only | Minimal — single assertion |
| `test_path_parse.pas` | One output name case | Minimal — single assertion |
| `test_cli_mode_matrix.pas` | All codecs, profiles, deblock, all 5 audio norms, loudnorm2 metrics, output extensions | **Good** — comprehensive command builder coverage |
| `run_apple_m4v_test.pas` | Apple M4V end-to-end | Requires real input file + MP4Box |
| `test_cli_args_matrix.sh` | CLI parser/summary path for representative option combinations | Good — verifies parse + summary flow |
| `check_gui_cli_issues.sh` | Integration: overwrite, peak2, loudnorm2, Apple M4V | Good — regression checks with real ffmpeg |

**Missing test coverage:**
- `converter_analysis.pas` — no unit tests for peak/loudnorm parsing logic
- `loudnorm_json.pas` — no unit tests for JSON metric extraction
- `process_utils.pas` — no unit tests
- `time_utils.pas` — no unit tests for `ParseFfmpegTime` / `FormatEta`
- `fs_utils.pas` — no unit tests
- `path_utils.pas` — `QuoteForShell` not tested with special characters
- Progress parsing — not tested (because it's not implemented)

---

## 7. Build System Assessment

### Documented Build Targets (from `fpc/README.md`):

```bash
make -C fpc/build cli       # CLI binary
make -C fpc/build lib       # Shared library (.so)
make -C fpc/build tests     # Test programs
```

### Actual State:

**`fpc/build/` directory does not exist** in the repository. The `README.md` and `PROJECT_OVERVIEW_DETAILED.md` both reference `fpc/build/Makefile` and `fpc/build/fpmake.pp`, but neither file is present in the checked-out tree.

### Alternative Build Methods:

1. **Manual FPC compilation** (documented in `fpc/DESCRIPTION.md`):
   ```bash
   fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json -Fu./fpc/cli ./fpc/cli/ffmpeg_converter.lpr
   fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json ./fpc/test/test_cmd_builder.pas
   ```

2. **Lazarus GUI** builds via `form.lpi` which correctly sets:
   ```xml
   <OtherUnitFiles Value="../converter;../common;../json"/>
   ```

### Build System Issues:

| ID | Severity | Description |
|----|----------|-------------|
| BUILD-1 | Functional | **`fpc/build/Makefile` is missing.** Documented `make` commands will fail. |
| BUILD-2 | Functional | **`fpc/build/fpmake.pp` is missing.** Referenced in docs but absent. |
| BUILD-3 | Info | **No shared library build is verified.** `converter_pas.lpr` exists but no Makefile target exercises it. |

---

## 8. Code Duplication

| Duplicated Code | Files |
|-----------------|-------|
| `SetAnsiField` (identical implementation) | `cli_args.pas`, `cli_menu.pas`, `form.pas`, `test_cli_mode_matrix.pas` — 4 copies |
| `ArrToStr` (identical implementation) | `converter_core.pas`, `converter_cmd_builder.pas`, `cli_args.pas` — 3 copies |
| `FormatEta` | `time_utils.pas`, `form.pas` — 2 copies (slightly different signatures) |

---

## 9. All Issues — Consolidated by Severity

### Critical (should fix before production use)

| ID | Component | Description |
|----|-----------|-------------|
| PATH-1 | `path_utils.pas` | `QuoteForShell` only escapes `"` — `$`, `` ` ``, `\`, `!` not handled. Shell injection / incorrect behavior risk. |
| PROC-1 | `process_utils.pas` | `TProcess` with `poWaitOnExit` + `poUsePipes` can deadlock on large output (>64KB pipe buffer). |

### Functional (affects correctness or feature completeness)

| ID | Component | Description |
|----|-----------|-------------|
| RUN-3 | `converter_runner.pas` | Real-time progress reporting during encoding is non-functional. Progress jumps 0% → 100%. |
| ANA-1 | `converter_analysis.pas` | `RunPeakTwoPass` does not check ffmpeg exit code. |
| RUN-1 | `converter_runner.pas` | `ProbeDuration` uses raw quoting instead of `QuoteForShell`. |
| BUILD-1 | Build system | `fpc/build/Makefile` is missing from repository. |
| BUILD-2 | Build system | `fpc/build/fpmake.pp` is missing from repository. |

### Minor (design quality, maintainability)

| ID | Component | Description |
|----|-----------|-------------|
| CORE-1 | `converter_core.pas` | StopFlag data race (no atomic). Works on x86 in practice. |
| CORE-2 | `cli_menu.pas` | `RunMenu` zeroes options record, discarding caller defaults. |
| CORE-3 | `converter_core.pas` | Batch return always `ERR_OK` on partial failure. |
| GUI-1 | `form.pas` | `GMainForm` global can be used after form destruction. |

### Info (low-risk, good practice improvements)

| ID | Component | Description |
|----|-----------|-------------|
| RUN-2 | `converter_runner.pas` | `ProbeDuration` is dead code. |
| ANA-2 | `converter_analysis.pas` | Hardcoded `/tmp/` diagnostic paths. |
| JSON-1 | `loudnorm_json.pas` | Non-idiomatic `J.Free` placement (should use `try..finally`). |
| M4V-1 | `apple_m4v_creator.pas` | `CleanupWorkDir` uses `rm -rf` via shell. |
| M4V-2 | `apple_m4v_creator.pas` | `Randomize` called per invocation. |
| GUI-2 | `form.pas` | `PAnsiChar` array from thread-local `FFiles` — fragile pattern. |
| CMD-1 | `converter_cmd_builder.pas` | Deblock silently ignored for h265_mi50. |

---

## 10. Overall Assessment

The Pascal port is **well-structured and functionally complete** for its stated scope.

**Strengths:**
- Faithful C API parity — types, enums, callbacks, and function signatures match exactly.
- Clean module separation (types / core / builder / analysis / runner / API surface).
- GUI is a meaningful addition with proper threading and async UI updates.
- Apple M4V creator is a substantial feature with multi-step pipeline and error handling.
- `test_cli_mode_matrix.pas` provides excellent command-builder test coverage.
- CLI interactive menu is fully functional with proper navigation and memory management.

**Key Weaknesses:**
- Shell quoting implementation has a security gap.
- Real-time progress reporting is not wired up.
- Build system Makefile is missing.
- Several utility functions are duplicated across 3–4 files.
- Test coverage gaps exist for analysis, JSON, and utility modules.
