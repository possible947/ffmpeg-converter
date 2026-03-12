# Pascal Branch — Refactoring Plan

**Date:** 2026-03-12
**Based on:** `fpc/REVIEW_REPORT.md`
**Goal:** Address all identified issues in priority order, improve maintainability, and complete missing infrastructure.

---

## Phase 1: Critical Fixes

> Fix security and reliability issues that can cause incorrect behavior or hangs.

### 1.1 Fix `QuoteForShell` — shell injection prevention
- **Issue:** PATH-1
- **File:** `common/path_utils.pas`
- **Action:** Replace the current double-quote-only escaping with single-quote wrapping strategy.
  - Wrap the string in single quotes: `'...'`
  - Escape embedded single quotes as `'\''`
  - Single-quoted strings in bash interpret nothing (`$`, `` ` ``, `\`, `!` are all literal).
- **Validation:** Add unit test `test/test_quote_shell.pas` with cases:
  - Normal filename: `input.mov`
  - Spaces: `my file.mov`
  - Double quotes: `my "file".mov`
  - Dollar sign: `test$HOME.mov`
  - Backtick: `` test`cmd`.mov ``
  - Backslash: `test\.mov`
  - Single quote: `test'file.mov`
  - Combined special chars

### 1.2 Fix `RunCommandCapture` — pipe deadlock prevention
- **Issue:** PROC-1
- **File:** `common/process_utils.pas`
- **Action:** Replace `poWaitOnExit` with an incremental read loop:
  1. Remove `poWaitOnExit` from `P.Options`.
  2. After `P.Execute`, loop: read available bytes from `P.Output` into a `TStringStream` while `P.Running`.
  3. After loop, drain any remaining bytes.
  4. Call `P.WaitOnExit` explicitly after all output is consumed.
  5. Set `Result.ExitCode := P.ExitStatus`.
- **Validation:** Manually test with a long encode that produces >64KB of combined output.

---

## Phase 2: Functional Fixes

> Fix issues that affect feature correctness or build usability.

### 2.1 Implement real-time progress reporting
- **Issue:** RUN-3
- **File:** `converter/converter_runner.pas`
- **Action:**
  1. Create a new `RunEncodeWithProgress` function that:
     - Launches ffmpeg via `TProcess` (no `poWaitOnExit`).
     - Reads output line-by-line in a loop.
     - Parses `out_time_ms=`, `fps=`, `progress=end` tokens from `-progress pipe:1` output.
     - Accepts a progress callback (or the full `TConverterCallbacks` record pointer + input file duration).
     - Computes percent from `out_time_ms / duration * 100`.
     - Computes ETA from elapsed time and percent.
  2. Add `ProbeDuration` call before encoding in `converter_core.pas`.
  3. Replace `RunEncode` call in `converter_core.pas` with `RunEncodeWithProgress`.
  4. Remove the synthetic 100% `on_progress_encode` call from `converter_core.pas` (the runner now handles it).
- **Depends on:** Phase 1.2 (safe process reading pattern).
- **Validation:** Run CLI with a real file and verify incremental progress output.

### 2.2 Add exit code check to `RunPeakTwoPass`
- **Issue:** ANA-1
- **File:** `converter/converter_analysis.pas`
- **Action:** After `RunCommandCapture`, check `R.ExitCode <> 0` and return `ERR_PEAK_ANALYSIS_FAILED` before inspecting the output text. Match the pattern already used in `RunLoudnormTwoPass`.
- **Validation:** Add to existing test script: force a peak analysis failure and verify error propagation.

### 2.3 Fix `ProbeDuration` quoting
- **Issue:** RUN-1
- **File:** `converter/converter_runner.pas`
- **Action:** Replace `'"' + InputFile + '"'` with `QuoteForShell(InputFile)`. Add `path_utils` to the `uses` clause.
- **Validation:** Covered by the new `QuoteForShell` tests.

### 2.4 Create build system (`fpc/build/Makefile`)
- **Issues:** BUILD-1, BUILD-2
- **Directory:** `fpc/build/`
- **Action:** Create `fpc/build/Makefile` with targets:

  ```makefile
  ROOT = ../..
  FPC_FLAGS = -Fu$(ROOT)/fpc/converter -Fu$(ROOT)/fpc/common -Fu$(ROOT)/fpc/json

  cli:
      fpc $(FPC_FLAGS) -Fu$(ROOT)/fpc/cli $(ROOT)/fpc/cli/ffmpeg_converter.lpr

  lib:
      fpc $(FPC_FLAGS) $(ROOT)/fpc/converter/converter_pas.lpr

  tests:
      fpc $(FPC_FLAGS) $(ROOT)/fpc/test/test_cmd_builder.pas
      fpc $(FPC_FLAGS) $(ROOT)/fpc/test/test_path_parse.pas
      fpc $(FPC_FLAGS) $(ROOT)/fpc/test/test_cli_mode_matrix.pas
      fpc $(FPC_FLAGS) -Fu$(ROOT)/fpc/cli $(ROOT)/fpc/test/run_apple_m4v_test.pas

  all: cli lib tests

  clean:
      find $(ROOT)/fpc -name '*.o' -o -name '*.ppu' | xargs rm -f
      rm -f $(ROOT)/fpc/cli/ffmpeg_converter
      rm -f $(ROOT)/fpc/converter/libconverter_pas.so
  ```

- **Validation:** Run `make -C fpc/build all` and `make -C fpc/build clean` successfully.

---

## Phase 3: Code Deduplication

> Extract duplicated code into shared units.

### 3.1 Extract `SetAnsiField` and `ArrToStr` to a shared unit
- **Files affected:** `cli_args.pas`, `cli_menu.pas`, `form.pas`, `test_cli_mode_matrix.pas`, `converter_core.pas`, `converter_cmd_builder.pas`
- **Action:**
  1. Create `common/ansichar_utils.pas` with:
     - `procedure SetAnsiField(var Dest: array of AnsiChar; const S: string);`
     - `function ArrToStr(const A: array of AnsiChar): string;`
  2. Replace all local copies with `uses ansichar_utils`.
  3. Update Makefile, LPI search paths, and manual build commands.
- **Validation:** All tests still pass. `grep -rn 'procedure SetAnsiField' fpc/` shows only `ansichar_utils.pas`.

### 3.2 Consolidate `FormatEta` duplication
- **Files affected:** `common/time_utils.pas`, `gui/form.pas`
- **Action:** Remove the local `FormatEta` from `form.pas` and add `time_utils` to the GUI `uses` clause. Adjust the signature if needed (the GUI version takes `Single`, the common version takes `Double` — unify to `Double`).
- **Validation:** GUI compiles and displays ETA correctly.

---

## Phase 4: Minor & Design Improvements

### 4.1 Make `StopFlag` atomic
- **Issue:** CORE-1
- **File:** `converter/converter_core.pas`
- **Action:** Replace `StopFlag: LongInt` with `InterlockedExchange` / `InterlockedCompareExchange` for writes/reads. Or use `volatile` + `ReadBarrier`/`WriteBarrier` if targeting FPC >= 3.2.
- **Validation:** No functional change — correctness under memory model.

### 4.2 Fix `RunMenu` options zeroing
- **Issue:** CORE-2
- **File:** `cli/cli_menu.pas`
- **Action:** Replace `FillChar(Opts, SizeOf(Opts), 0)` at the start of `RunMenu` with `InitDefaultOptions(Opts)`. This preserves new defaults if fields are added.
- **Validation:** Run interactive menu and verify default codec/profile/audio_norm values.

### 4.3 Guard `GMainForm` against use-after-free
- **Issue:** GUI-1
- **File:** `gui/form.pas`
- **Action:**
  1. Add `FormDestroy` handler that sets `GMainForm := nil`.
  2. In `QueueLog`, `QueueStatus`, etc., the existing `if Assigned(GMainForm)` check already guards this. Just ensure the assignment is done.
  3. In `StopClicked`, call `converter_stop` and then wait for `FWorker` to finish (e.g., `FWorker.WaitFor` with a timeout) before allowing form destruction.
- **Validation:** Close GUI during active conversion — no crash.

### 4.4 Fix `loudnorm_json.pas` cleanup pattern
- **Issue:** JSON-1
- **File:** `json/loudnorm_json.pas`
- **Action:** Move `J.Free` into a `try..finally` block:
  ```pascal
  J := nil;
  try
    try
      J := GetJSON(Text);
      ...
      Result := True;
    except
      Result := False;
    end;
  finally
    J.Free;
  end;
  ```
- **Validation:** Existing JSON test cases still pass.

### 4.5 Remove dead code `ProbeDuration`
- **Issue:** RUN-2
- **Note:** If Phase 2.1 (progress reporting) is completed first, `ProbeDuration` will become used code. If Phase 2.1 is skipped, remove the function.
- **Decision:** Keep — it will be used in Phase 2.1.

### 4.6 Use `GetTempDir` for diagnostic logs
- **Issue:** ANA-2
- **File:** `converter/converter_analysis.pas`
- **Action:** Replace hardcoded `/tmp/` paths with `GetTempDir(False) + 'ffc_peak_fail.log'` and similar.
- **Validation:** Build and run analysis — verify log files are created in the system temp directory.

---

## Phase 5: Test Coverage Expansion

### 5.1 Add `test_loudnorm_json.pas`
- **Coverage gap:** `loudnorm_json.pas` has no unit tests.
- **Action:** Create test program that:
  - Parses a valid JSON string with known metric values → assert correctness.
  - Parses invalid JSON → assert `False` return.
  - Parses JSON with missing keys → assert `False` return.
  - Parses JSON with non-numeric string values → assert `False` return.

### 5.2 Add `test_time_utils.pas`
- **Coverage gap:** `time_utils.pas` has no unit tests.
- **Action:** Create test program that:
  - `ParseFfmpegTime('01:23:45.678')` → assert `5025.678`.
  - `ParseFfmpegTime('00:00:00.000')` → assert `0.0`.
  - `FormatEta(3661.0)` → assert contains `01:01:01`.
  - `FormatEta(0)` → assert contains `--:--:--`.

### 5.3 Add `test_quote_shell.pas`
- **Coverage gap:** `QuoteForShell` not tested with special characters.
- **Action:** Created in Phase 1.1 (see above).

### 5.4 Expand `test_cmd_builder.pas`
- **Current state:** Single assertion (command prefix check).
- **Action:** Merge its scope into `test_cli_mode_matrix.pas` or expand it to test at least one case per codec. Since `test_cli_mode_matrix.pas` already covers this comprehensively, consider removing the minimal test to avoid redundancy.

### 5.5 Update Makefile `tests` target
- **Action:** Add all new test programs to the `tests` target in `fpc/build/Makefile`.

---

## Phase 6: Documentation Updates

### 6.1 Update `fpc/README.md`
- Remove references to `fpc/build/fpmake.pp` (unless it's actually created).
- Verify all manual FPC compilation commands still work after refactoring.
- Update "Current Status" section to reflect progress reporting and new tests.

### 6.2 Update `fpc/DESCRIPTION.md`
- Update verification commands if unit search paths change.
- Add `common/ansichar_utils.pas` to the design notes.

### 6.3 Update `PROJECT_OVERVIEW_DETAILED.md`
- Remove mention of `fpc/build/fpmake.pp` if not created.
- Add mention of new test files.

---

## Execution Order Summary

| Phase | Priority | Effort | Depends On |
|-------|----------|--------|------------|
| 1.1 Fix QuoteForShell | P0 | Small | — |
| 1.2 Fix RunCommandCapture deadlock | P0 | Medium | — |
| 2.4 Create Makefile | P1 | Small | — |
| 2.2 Peak analysis exit code check | P1 | Small | — |
| 2.3 ProbeDuration quoting | P1 | Tiny | 1.1 |
| 3.1 Extract SetAnsiField/ArrToStr | P1 | Small | 2.4 |
| 3.2 Consolidate FormatEta | P1 | Tiny | — |
| 2.1 Progress reporting | P2 | Large | 1.2, 2.3 |
| 4.1–4.6 Minor improvements | P2 | Small each | — |
| 5.1–5.5 Test expansion | P2 | Medium total | 1.1, 2.4 |
| 6.1–6.3 Documentation | P3 | Small | All above |

---

## Acceptance Criteria

- [ ] `make -C fpc/build all` compiles CLI, library, and all tests without errors.
- [ ] `make -C fpc/build tests` runs and all test programs exit with code 0.
- [ ] `QuoteForShell` passes all special-character test cases.
- [ ] CLI conversion of a real file shows incremental progress (not 0% → 100% jump).
- [ ] No `SetAnsiField` or `ArrToStr` duplicates remain outside `common/ansichar_utils.pas`.
- [ ] GUI can be closed during active conversion without crash.
- [ ] `grep -rn 'procedure SetAnsiField' fpc/` returns exactly 1 result.
- [ ] All documentation references to build commands are accurate and functional.
