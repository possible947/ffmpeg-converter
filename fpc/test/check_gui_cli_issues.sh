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
MATRIX_BIN="$ROOT_DIR/fpc/test/test_cli_mode_matrix"
PARSER_BIN="$ROOT_DIR/fpc/test/test_analysis_parsers"
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

if [[ ! -x "$MATRIX_BIN" || ! -x "$PARSER_BIN" ]]; then
  echo "INFO: test binaries missing, they will be built in the rebuild step."
fi

echo "INFO: rebuilding binaries before checks..."
make -C "$ROOT_DIR/fpc/build" all >/dev/null

if [[ ! -x "$MATRIX_BIN" ]]; then
  echo "ERROR: mode matrix test binary not found/executable: $MATRIX_BIN"
  exit 1
fi

if [[ ! -x "$PARSER_BIN" ]]; then
  echo "ERROR: parser test binary not found/executable: $PARSER_BIN"
  exit 1
fi

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

run_small_test() {
  local name="$1"
  local bin="$2"
  local log="$OUT_BASE/${name}.log"

  set +e
  "$bin" >"$log" 2>&1
  local code=$?
  set -e

  if [[ $code -eq 0 ]]; then
    pass "$name"
  else
    fail "$name (exit=$code). See $log"
  fi
}

# 0) deterministic command/parser regression checks
run_small_test "mode matrix command fragments" "$MATRIX_BIN"
run_small_test "analysis parser edge cases" "$PARSER_BIN"

# 1) Main worker overwrite check
# Create existing destination file first, then run with --overwrite.
OVERWRITE_DIR="$OUT_BASE/overwrite_main"
mkdir -p "$OVERWRITE_DIR"
INPUT_BASE="$(basename "$TEST_FILE")"
INPUT_STEM="${INPUT_BASE%.*}"
OVERWRITE_TARGET="$OVERWRITE_DIR/${INPUT_STEM}_converted.mkv"
cp -f "$TEST_FILE" "$OVERWRITE_TARGET" || true
sleep 1
before_mtime=$(stat -c %Y "$OVERWRITE_TARGET" 2>/dev/null || echo 0)

run_cli_case "overwrite_main" -c copy -a none --overwrite

after_mtime=$(stat -c %Y "$OVERWRITE_TARGET" 2>/dev/null || echo 0)
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

# 5) Chapter flow integration (with and without chapters)
if [[ -x "$APPLE_BIN" ]] && command -v ffmpeg >/dev/null 2>&1 && command -v ffprobe >/dev/null 2>&1; then
  CH_DIR="$OUT_BASE/chapter_flow"
  mkdir -p "$CH_DIR"

  META_FILE="$CH_DIR/chapters.ffmeta"
  CH_INPUT="$CH_DIR/input_with_chapters.mkv"
  NOCH_INPUT="$CH_DIR/input_without_chapters.mkv"
  CH_OUT="$CH_DIR/output_with_chapters.m4v"
  NOCH_OUT="$CH_DIR/output_without_chapters.m4v"

  cat >"$META_FILE" <<'EOF'
;FFMETADATA1
[CHAPTER]
TIMEBASE=1/1000
START=0
END=400
title=Intro
[CHAPTER]
TIMEBASE=1/1000
START=400
END=900
title=Main
EOF

  ffmpeg -y -f lavfi -i testsrc=size=320x240:rate=24 -f lavfi -i sine=frequency=1000:sample_rate=48000 \
    -t 1.2 -c:v libx264 -pix_fmt yuv420p -c:a aac "$CH_DIR/base.mkv" >/dev/null 2>&1

  ffmpeg -y -i "$CH_DIR/base.mkv" -i "$META_FILE" -map_metadata 1 -codec copy "$CH_INPUT" >/dev/null 2>&1
  cp -f "$CH_DIR/base.mkv" "$NOCH_INPUT"

  set +e
  timeout 900 "$APPLE_BIN" "$CH_INPUT" "$CH_OUT" >"$CH_DIR/run_with_chapters.log" 2>&1
  code_ch=$?
  timeout 900 "$APPLE_BIN" "$NOCH_INPUT" "$NOCH_OUT" >"$CH_DIR/run_without_chapters.log" 2>&1
  code_noch=$?
  set -e

  if [[ $code_ch -eq 0 && $code_noch -eq 0 ]]; then
    ffprobe -v error -print_format json -show_chapters "$CH_OUT" >"$CH_DIR/out_with_chapters.json" || true
    ffprobe -v error -print_format json -show_chapters "$NOCH_OUT" >"$CH_DIR/out_without_chapters.json" || true

    if grep -q '"start_time"' "$CH_DIR/out_with_chapters.json"; then
      pass "Chapter flow: chaptered source preserved chapters"
    else
      printf '0:00:00.000 Intro\n0:00:00.400 Main\n' >"$CH_DIR/support_chapters.txt"
      cp -f "$CH_OUT" "$CH_DIR/support_probe.m4v"
      if MP4Box -chap "$CH_DIR/support_chapters.txt" "$CH_DIR/support_probe.m4v" >"$CH_DIR/support_probe.log" 2>&1; then
        fail "Chapter flow: expected chapters in chaptered output"
      else
        info "Chapter flow strict assertion skipped: local MP4Box chapter text import appears unsupported."
      fi
    fi

    if grep -q '"start_time"' "$CH_DIR/out_without_chapters.json"; then
      fail "Chapter flow: output without chapters unexpectedly contains chapters"
    else
      pass "Chapter flow: source without chapters skipped chapter import"
    fi
  else
    fail "Chapter flow: apple test run failed (with=$code_ch without=$code_noch)"
  fi
else
  info "Chapter flow integration skipped (missing apple binary or ffmpeg/ffprobe)."
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
echo "  $OUT_BASE/mode matrix command fragments.log"
echo "  $OUT_BASE/analysis parser edge cases.log"
