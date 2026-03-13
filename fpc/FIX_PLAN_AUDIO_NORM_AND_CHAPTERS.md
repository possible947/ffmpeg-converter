# FPC Fix Plan: Chapters + Audio Normalization (Deep Audit)

## Goal
Stabilize and correct Pascal behavior for:
1. Chapter extraction/import in Apple M4V flow (parity with fixed shell script behavior).
2. Audio normalization logic (`none`, `peak_norm`, `peak_norm_2pass`, `loudness_norm`, `loudness_norm_2pass`) including parameter correctness and two-pass analysis pipeline.

## Why This Plan
Known symptoms indicate prior patches around chapter extraction and audio normalization are not reliable. This plan focuses on full logical validation and controlled refactor, not partial edits.

## Strict Migration Rule (Mandatory)
- Source of truth for normalization behavior is the C implementation in `src/`.
- Before changing Pascal logic, map every relevant C parameter and decision branch to Pascal equivalents.
- For each mode (`none`, `peak_norm`, `peak_norm_2pass`, `loudness_norm`, `loudness_norm_2pass`), preserve C semantics for:
  - target values and computed values
  - ffmpeg argument ordering and filter composition
  - error handling and skip/fail behavior
  - per-file state reset behavior in batch processing
- Any intentional deviation from C behavior is forbidden unless all conditions are met:
  - deviation is documented in the commit and in this plan
  - rationale includes reproducible evidence (test/log/media case)
  - matching tests are added that demonstrate why deviation is required

## Scope
- In scope:
  - `fpc/converter/apple_m4v_creator.pas`
  - `fpc/converter/converter_analysis.pas`
  - `fpc/converter/converter_cmd_builder.pas`
  - `fpc/converter/converter_core.pas`
  - `fpc/json/loudnorm_json.pas`
  - `fpc/test/*.pas`, `fpc/test/*.sh` (tests for regression coverage)
- Out of scope:
  - C implementation (`src/`) changes
  - GUI-only visual behavior changes

## Workstream A: Chapter Extraction Parity (Apple M4V)

### A1. Command parity change
- Update Pascal chapter probe call to match fixed shell behavior:
  - `ffprobe -v error -print_format json -show_chapters <input> > <workdir>/chapters.json`
- In Pascal implementation, ensure equivalent semantics:
  - command succeeds/fails by ffprobe exit code
  - JSON is written to file path in workdir
  - chapter parser reads from file content (single source of truth)

### A2. Robustness checks
- Validate behavior when:
  - no chapters exist (`chapters` missing/empty)
  - malformed JSON
  - ffprobe returns non-zero
  - UTF-8 chapter titles present
- Ensure chapter import step only runs when `chapters.txt` has valid content.

### A3. Data-path cleanup
- Remove duplicate chapter data paths (stdout string vs file copy) to avoid divergence.
- Keep one canonical flow:
  - ffprobe -> `chapters.json` -> parse -> `chapters.txt` -> `MP4Box -chap`.

## Workstream B: Deep Audit of Audio Normalization Parameters and Logic

### B0. Required audit matrix (must be completed before code rewrite)
For each mode, verify command inputs, expected ffmpeg filters, and final behavior:
- `none`
- `peak_norm`
- `peak_norm_2pass`
- `loudness_norm`
- `loudness_norm_2pass`

### B1. Parameter consistency audit
- Verify defaults and write points of:
  - `I_target`, `TP_target`, `LRA_target`
  - `measured_I`, `measured_TP`, `measured_LRA`, `measured_thresh`, `measured_offset`
  - `gain`
  - `genre` mapping in `ApplyGenreTargets`
- Confirm no stale values leak across files in batch mode.
- Confirm float formatting is locale-safe (`.` decimal separator) across all generated ffmpeg commands.

### B2. Peak 2-pass logic (`RunPeakTwoPass`)
- Validate parsing source (`volumedetect`) and token extraction strategy.
- Confirm expected gain policy (currently `-3.0 - max_volume`) and clipping safety.
- Add strict checks for missing/NaN parse results.
- Ensure failures always return `ERR_PEAK_ANALYSIS_FAILED` with actionable debug dump.

### B3. Loudnorm 2-pass logic (`RunLoudnormTwoPass`)
- Verify first-pass command structure and target parameters.
- Replace brittle JSON slicing (`last { ... last }`) with deterministic extraction of loudnorm JSON block.
- Confirm `TryParseLoudnormJson` captures all required keys for second pass.
- Validate mapping of parsed metrics to option fields used in final command builder.
- Ensure second-pass command contains complete measured fields and `linear=true` only when valid.

### B4. Command builder correctness (`BuildFfmpegCommand`)
- Validate `-af` chain generation for every audio mode.
- Confirm loudnorm one-pass and two-pass settings are internally consistent with analysis outputs.
- Confirm no conflict between codec-specific audio behavior (`use_aac_for_h265`) and normalization filters.

### B5. Core flow integrity (`converter_core`)
- Validate stage order for each file:
  - option setup -> analysis (if 2-pass) -> command build -> encode
- Ensure callbacks report analysis/encoding stages correctly.
- Confirm failed analysis skips encode for that file and does not corrupt next-file state.

## Workstream C: Test Plan (must be expanded before merge)

### C1. Unit tests
- Add/extend tests for:
  - robust loudnorm JSON extraction/parser cases
  - peak token parsing edge cases
  - command builder output for all audio modes and numeric fields

### C2. Integration tests
- Extend `fpc/test/check_gui_cli_issues.sh` coverage:
  - explicit checks for peak2/loudnorm2 failure regressions
  - assertions on generated command fragments
- Add chapter-flow integration test:
  - source with chapters -> confirm `chapters.txt` generated and imported
  - source without chapters -> confirm graceful skip

### C3. Negative tests
- ffprobe unavailable
- ffmpeg unavailable
- invalid input file
- malformed JSON and partial loudnorm output

## Implementation Sequence
1. Chapter flow cleanup in `apple_m4v_creator.pas`.
2. Loudnorm/peak analysis parser hardening in `converter_analysis.pas`.
3. Command generation alignment in `converter_cmd_builder.pas`.
4. Core-state and callback verification in `converter_core.pas`.
5. Tests and regression scripts update.
6. Final end-to-end run on representative media set.

## Acceptance Criteria
- Chapter flow in Pascal matches shell fix semantics and no longer relies on mixed stdout/file paths.
- `peak_norm_2pass` and `loudness_norm_2pass` produce stable, deterministic commands with valid measured values.
- No known regression in `none`, `peak_norm`, or `loudness_norm` modes.
- Added tests fail on old logic and pass on new logic.
- Batch processing does not leak analysis state between files.

## Notes for Execution
- Prefer small, reviewable commits per workstream.
- Keep debug artifacts (`/tmp/ffc_peak_fail.log`, `/tmp/ffc_loud_fail.log`) but standardize write conditions and content.
- Re-check both CLI and GUI paths since both route through converter core.
