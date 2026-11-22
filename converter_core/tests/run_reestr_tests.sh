#!/bin/bash
# Tests for the converter_core registry (GetReestr), macOS/Linux.
# Builds tests/reestr_tests.pas into a temporary folder with heap and range
# checks and runs it. Fails on any FAIL line or unfreed memory.
#
#   converter_core/tests/run_reestr_tests.sh
#   converter_core/tests/run_reestr_tests.sh --dump linux_nvenc7 linux_vulkan_nvidia
#   converter_core/tests/run_reestr_tests.sh --detected

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CORE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FPC="${FPC:-fpc}"

if ! command -v "$FPC" >/dev/null 2>&1; then
  echo "NOT AVAILABLE  fpc not found (set FPC)"
  exit 2
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# -gh: report memory leaks; -Cr -Co -Ct: range, overflow and stack checks
if ! "$FPC" -Mobjfpc -Sh -gh -gl -Cr -Co -Ct -vew "-Fu$CORE_DIR/src" "-FU$WORK" \
    "-o$WORK/reestr_tests" "$SCRIPT_DIR/reestr_tests.pas" >"$WORK/build.log" 2>&1; then
  cat "$WORK/build.log"
  echo "FAIL  build"
  exit 1
fi

case "${1:-}" in
  --dump|--detected) "$WORK/reestr_tests" "$@"; exit $? ;;
esac

OUT="$("$WORK/reestr_tests" 2>&1)"
CODE=$?
printf '%s\n' "$OUT" | grep -E '^(PASS|FAIL|INFO)|passed,'

LEAK="$(printf '%s\n' "$OUT" | grep -E '^[1-9][0-9]* unfreed memory blocks')"
if [ -n "$LEAK" ]; then
  echo "FAIL  memory: $LEAK"
  exit 1
fi
exit $CODE
