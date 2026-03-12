#!/usr/bin/env bash
set -euo pipefail

# Quick verification script for reported issues:
# 1) Main worker overwrite behavior
# 2) Two-pass audio normalization modes (peak2, loudnorm2)
# 3) Apple M4V overwrite behavior via test harness
#
# Usage:
#   ./fpc/test/check_gui_cli_issues.sh [path/to/test.mp4]

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_FILE="${1:-$ROOT_DIR/test.mp4}"
CLI_BIN="$ROOT_DIR/fpc/cli/ffmpeg_converter"
APPLE_BIN="$ROOT_DIR/fpc/test/run_apple_m4v_test"
OUT_BASE="/tmp/ffc_user_check_$(date +%Y%m%d_%H%M%S)"

mkdir -p "$OUT_BASE"

if [[ ! -f "$TEST_FILE" ]]; then
  echo "ERROR: test file not found: $TEST_FILE"
  exit 1
fi

if [[ ! -x "$CLI_BIN" ]]; then
  echo "ERROR: cli binary not found/executable: $CLI_BIN"
  echo "Build first: make -C $ROOT_DIR/fpc/build all"
  exit 1
fi

echo "INFO: rebuilding binaries before checks..."
make -C "$ROOT_DIR/fpc/build" all >/dev/null

rm -f /tmp/ffc_peak_fail.log /tmp/ffc_loud_fail.log

if [[ ! -x "$APPLE_BIN" ]]; then
  echo "WARN: apple test binary not found/executable: $APPLE_BIN"
  echo "Apple overwrite check will be skipped."
fi

pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*"; }
info() { echo "INFO: $*"; }

run_cli_case() {
  local name="$1"
  shift
  local out_dir="$OUT_BASE/$name"
  local log="$out_dir/run.log"
  mkdir -p "$out_dir"

  set +e
  timeout 420 "$CLI_BIN" "$@" -o "$out_dir" "$TEST_FILE" >"$log" 2>&1
  local code=$?
  set -e

  echo "$code" > "$out_dir/exit.code"
  info "$name log: $log"
  info "$name exit: $code"
}

# 1) Main worker overwrite check
# Create existing destination file first, then run with --overwrite.
OVERWRITE_DIR="$OUT_BASE/overwrite_main"
mkdir -p "$OVERWRITE_DIR"
cp -f "$TEST_FILE" "$OVERWRITE_DIR/test_converted.mkv" || true
sleep 1
before_mtime=$(stat -c %Y "$OVERWRITE_DIR/test_converted.mkv" 2>/dev/null || echo 0)

run_cli_case "overwrite_main" -c copy -a none --overwrite

after_mtime=$(stat -c %Y "$OUT_BASE/overwrite_main/test_converted.mkv" 2>/dev/null || echo 0)
if grep -qi "output file exists - skipping" "$OUT_BASE/overwrite_main/run.log"; then
  fail "Main overwrite: still skipped existing output."
elif (( after_mtime > before_mtime )); then
  pass "Main overwrite: existing file was replaced."
else
  fail "Main overwrite: output mtime did not change."
fi

# 2) Two-pass peak normalization check
run_cli_case "peak2" -c copy -a peak2 --overwrite
if grep -qi "peak analysis failed" "$OUT_BASE/peak2/run.log"; then
  fail "peak2: analysis failed."
else
  pass "peak2: no analysis-failed message detected."
fi
if [[ -f /tmp/ffc_peak_fail.log ]]; then
  cp -f /tmp/ffc_peak_fail.log "$OUT_BASE/peak2/analysis_dump.log"
  info "peak2 analysis dump: $OUT_BASE/peak2/analysis_dump.log"
fi

# 3) Two-pass loudnorm normalization check
run_cli_case "loudnorm2" -c copy -a loudnorm2 -g rock --overwrite
if grep -qi "loudnorm analysis failed" "$OUT_BASE/loudnorm2/run.log"; then
  fail "loudnorm2: analysis failed."
else
  pass "loudnorm2: no analysis-failed message detected."
fi
if [[ -f /tmp/ffc_loud_fail.log ]]; then
  cp -f /tmp/ffc_loud_fail.log "$OUT_BASE/loudnorm2/analysis_dump.log"
  info "loudnorm2 analysis dump: $OUT_BASE/loudnorm2/analysis_dump.log"
fi

# 4) Apple M4V overwrite check (test harness)
if [[ -x "$APPLE_BIN" ]]; then
  APPLE_OUT="$OUT_BASE/apple_overwrite_test.m4v"
  APPLE_LOG1="$OUT_BASE/apple_run1.log"
  APPLE_LOG2="$OUT_BASE/apple_run2.log"

  set +e
  timeout 900 "$APPLE_BIN" "$TEST_FILE" "$APPLE_OUT" >"$APPLE_LOG1" 2>&1
  code1=$?
  timeout 900 "$APPLE_BIN" "$TEST_FILE" "$APPLE_OUT" >"$APPLE_LOG2" 2>&1
  code2=$?
  set -e

  info "apple run1 log: $APPLE_LOG1 (exit=$code1)"
  info "apple run2 log: $APPLE_LOG2 (exit=$code2)"

  if [[ $code1 -eq 0 && $code2 -eq 0 ]]; then
    pass "Apple overwrite (test harness): second write succeeded on same output path."
  else
    fail "Apple overwrite (test harness): one of runs failed."
  fi
else
  info "Apple overwrite check skipped (binary missing)."
fi

echo
echo "Done. Output folder: $OUT_BASE"
echo "Send me these logs if something fails:"
echo "  $OUT_BASE/overwrite_main/run.log"
echo "  $OUT_BASE/peak2/run.log"
echo "  $OUT_BASE/peak2/analysis_dump.log"
echo "  $OUT_BASE/loudnorm2/run.log"
echo "  $OUT_BASE/loudnorm2/analysis_dump.log"
echo "  $OUT_BASE/apple_run1.log"
echo "  $OUT_BASE/apple_run2.log"
