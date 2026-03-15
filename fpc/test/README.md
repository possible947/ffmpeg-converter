# FPC Tests

This folder contains parity-oriented tests for the Pascal port.

## Priority Tests

1. Command builder parity against C output for fixed option sets
2. Path parsing behavior with spaces/quotes/escapes
3. Error code mapping parity
4. Basic process runner checks for ffmpeg/ffprobe invocation

## Test Binaries

- `test_cmd_builder`
- `test_cli_mode_matrix`
- `test_analysis_parsers`
- `test_path_parse`
- `run_apple_m4v_test`

Build all test binaries:

```bash
make -C fpc/build tests
```

## Full Regression Run

Use the all-in-one runner from repository root:

```bash
./fpc/test/run_all_regression_and_capture.sh
```

It executes builds, unit tests, integration checks, and Pascal-vs-C parity checks.
Results are saved under `/tmp/ffc_regression_<timestamp>/`.
