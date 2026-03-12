#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLI_BIN="$ROOT_DIR/cli/ffmpeg_converter"

if [[ ! -x "$CLI_BIN" ]]; then
  echo "FAIL: CLI binary not found: $CLI_BIN"
  exit 1
fi

run_case() {
  local name="$1"
  shift
  local out
  set +e
  out="$($CLI_BIN "$@" 2>&1)"
  local code=$?
  set -e

  if [[ $code -eq 0 ]]; then
    echo "FAIL [$name]: expected non-zero (no real conversion should happen in parser test)"
    echo "$out"
    exit 1
  fi

  if ! grep -q "=== Summary ===" <<<"$out"; then
    echo "FAIL [$name]: summary was not printed"
    echo "$out"
    exit 1
  fi

  if ! grep -q "Verifying files" <<<"$out"; then
    echo "FAIL [$name]: verify stage was not reached"
    echo "$out"
    exit 1
  fi

  echo "OK [$name]"
}

# Use a definitely missing file so parser/summary path is exercised without conversion.
MISSING_FILE="/tmp/ffmpeg_converter_missing_input_$$.mov"

run_case "copy/none" -c copy -a none "$MISSING_FILE"
run_case "prores/peak" -c prores -p hq -d weak -a peak "$MISSING_FILE"
run_case "prores_ks/loudnorm" -c prores_ks -p standard -d strong -a loudnorm "$MISSING_FILE"
if [[ "${RUN_H265_CASES:-0}" == "1" ]]; then
  run_case "h265/loudnorm2" -c h265_mi50 -a loudnorm2 -g rock --overwrite "$MISSING_FILE"
else
  echo "SKIP [h265/loudnorm2]: set RUN_H265_CASES=1 to enable on h265-capable host"
fi

echo "OK: CLI args matrix"
