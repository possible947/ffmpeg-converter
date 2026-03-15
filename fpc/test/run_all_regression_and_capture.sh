#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
TS="$(date +%Y%m%d_%H%M%S)"
RESULTS_DIR="/tmp/ffc_regression_${TS}"
LOG_DIR="$RESULTS_DIR/logs"
SUMMARY="$RESULTS_DIR/summary.txt"
STATUS_TSV="$RESULTS_DIR/status.tsv"

mkdir -p "$LOG_DIR"

PAS_CLI="$ROOT_DIR/fpc/cli/ffmpeg_converter"
C_CLI="$ROOT_DIR/build/src/cli/ffmpeg_converter"
TEST_MP4="$ROOT_DIR/test.mp4"

PASS_COUNT=0
FAIL_COUNT=0

printf "name\tstatus\texit_code\tlog\n" > "$STATUS_TSV"

run_step() {
  local name="$1"
  shift
  local log="$LOG_DIR/${name}.log"

  echo "[RUN] $name"
  "$@" >"$log" 2>&1
  local code=$?

  if [[ $code -eq 0 ]]; then
    echo "[PASS] $name"
    printf "%s\tPASS\t0\t%s\n" "$name" "$log" >> "$STATUS_TSV"
    PASS_COUNT=$((PASS_COUNT + 1))
    return 0
  fi

  echo "[FAIL] $name (exit=$code)"
  printf "%s\tFAIL\t%s\t%s\n" "$name" "$code" "$log" >> "$STATUS_TSV"
  FAIL_COUNT=$((FAIL_COUNT + 1))
  return 1
}

run_shell_step() {
  local name="$1"
  local cmd="$2"
  local log="$LOG_DIR/${name}.log"

  echo "[RUN] $name"
  bash -lc "$cmd" >"$log" 2>&1
  local code=$?

  if [[ $code -eq 0 ]]; then
    echo "[PASS] $name"
    printf "%s\tPASS\t0\t%s\n" "$name" "$log" >> "$STATUS_TSV"
    PASS_COUNT=$((PASS_COUNT + 1))
    return 0
  fi

  echo "[FAIL] $name (exit=$code)"
  printf "%s\tFAIL\t%s\t%s\n" "$name" "$code" "$log" >> "$STATUS_TSV"
  FAIL_COUNT=$((FAIL_COUNT + 1))
  return 1
}

# -----------------------------
# Build stage
# -----------------------------
run_step "build_pascal_tests" make -C "$ROOT_DIR/fpc/build" tests
run_step "build_pascal_cli" make -C "$ROOT_DIR/fpc/build" cli

if [[ ! -x "$C_CLI" ]]; then
  run_step "build_c_cli" cmake --build "$ROOT_DIR/build" --target ffmpeg_converter
fi

# -----------------------------
# Unit tests (Pascal)
# -----------------------------
run_step "test_cmd_builder" "$ROOT_DIR/fpc/test/test_cmd_builder"
run_step "test_cli_mode_matrix" "$ROOT_DIR/fpc/test/test_cli_mode_matrix"
run_step "test_analysis_parsers" "$ROOT_DIR/fpc/test/test_analysis_parsers"
run_step "test_path_parse" "$ROOT_DIR/fpc/test/test_path_parse"

# -----------------------------
# Integration smoke script
# -----------------------------
if [[ -f "$TEST_MP4" ]]; then
  run_shell_step "check_gui_cli_issues" "cd '$ROOT_DIR' && ./fpc/test/check_gui_cli_issues.sh '$TEST_MP4'"
else
  echo "[WARN] test.mp4 not found, skipping check_gui_cli_issues" | tee -a "$SUMMARY"
fi

# -----------------------------
# Filter parity test (Pascal vs C)
# peak2 + loudnorm2 on test.mp4-derived clip
# -----------------------------
PARITY_DIR="$RESULTS_DIR/parity"
mkdir -p "$PARITY_DIR/pas_peak" "$PARITY_DIR/c_peak" "$PARITY_DIR/pas_loud" "$PARITY_DIR/c_loud"

if [[ -f "$TEST_MP4" ]]; then
  run_shell_step "parity_make_clip" "ffmpeg -y -v error -ss 00:00:30 -i '$TEST_MP4' -t 00:00:40 -c copy '$PARITY_DIR/test_clip.mp4'"

  run_shell_step "parity_pas_peak2" "'$PAS_CLI' -c copy -a peak2 --overwrite -o '$PARITY_DIR/pas_peak' '$PARITY_DIR/test_clip.mp4'"
  run_shell_step "parity_c_peak2" "'$C_CLI' -c copy -a peak2 --overwrite -o '$PARITY_DIR/c_peak' '$PARITY_DIR/test_clip.mp4'"
  run_shell_step "parity_pas_loudnorm2" "'$PAS_CLI' -c copy -a loudnorm2 -g rock --overwrite -o '$PARITY_DIR/pas_loud' '$PARITY_DIR/test_clip.mp4'"
  run_shell_step "parity_c_loudnorm2" "'$C_CLI' -c copy -a loudnorm2 -g rock --overwrite -o '$PARITY_DIR/c_loud' '$PARITY_DIR/test_clip.mp4'"

  run_shell_step "parity_stream_md5" "\
    for f in '$PARITY_DIR/pas_peak/test_clip_converted.mkv' '$PARITY_DIR/c_peak/test_clip_converted.mkv' '$PARITY_DIR/pas_loud/test_clip_converted.mkv' '$PARITY_DIR/c_loud/test_clip_converted.mkv'; do \
      [[ -f \"\$f\" ]] || { echo \"missing file: \$f\"; exit 2; }; \
    done; \
    pas_peak_a=\$(ffmpeg -v error -i '$PARITY_DIR/pas_peak/test_clip_converted.mkv' -map 0:a:0 -f md5 - | sed 's/^MD5=//'); \
    c_peak_a=\$(ffmpeg -v error -i '$PARITY_DIR/c_peak/test_clip_converted.mkv' -map 0:a:0 -f md5 - | sed 's/^MD5=//'); \
    pas_peak_v=\$(ffmpeg -v error -i '$PARITY_DIR/pas_peak/test_clip_converted.mkv' -map 0:v:0 -c copy -f md5 - | sed 's/^MD5=//'); \
    c_peak_v=\$(ffmpeg -v error -i '$PARITY_DIR/c_peak/test_clip_converted.mkv' -map 0:v:0 -c copy -f md5 - | sed 's/^MD5=//'); \
    pas_loud_a=\$(ffmpeg -v error -i '$PARITY_DIR/pas_loud/test_clip_converted.mkv' -map 0:a:0 -f md5 - | sed 's/^MD5=//'); \
    c_loud_a=\$(ffmpeg -v error -i '$PARITY_DIR/c_loud/test_clip_converted.mkv' -map 0:a:0 -f md5 - | sed 's/^MD5=//'); \
    pas_loud_v=\$(ffmpeg -v error -i '$PARITY_DIR/pas_loud/test_clip_converted.mkv' -map 0:v:0 -c copy -f md5 - | sed 's/^MD5=//'); \
    c_loud_v=\$(ffmpeg -v error -i '$PARITY_DIR/c_loud/test_clip_converted.mkv' -map 0:v:0 -c copy -f md5 - | sed 's/^MD5=//'); \
    { \
      echo \"peak_audio_pas=\$pas_peak_a\"; \
      echo \"peak_audio_c=\$c_peak_a\"; \
      echo \"peak_video_pas=\$pas_peak_v\"; \
      echo \"peak_video_c=\$c_peak_v\"; \
      echo \"loud_audio_pas=\$pas_loud_a\"; \
      echo \"loud_audio_c=\$c_loud_a\"; \
      echo \"loud_video_pas=\$pas_loud_v\"; \
      echo \"loud_video_c=\$c_loud_v\"; \
    } > '$PARITY_DIR/md5.txt'; \
    [[ \"\$pas_peak_a\" == \"\$c_peak_a\" ]] && [[ \"\$pas_peak_v\" == \"\$c_peak_v\" ]] && [[ \"\$pas_loud_a\" == \"\$c_loud_a\" ]] && [[ \"\$pas_loud_v\" == \"\$c_loud_v\" ]]"

  run_shell_step "parity_metrics" "\
    { \
      echo 'peak2:'; \
      ffmpeg -nostdin -i '$PARITY_DIR/c_peak/test_clip_converted.mkv' -af volumedetect -f null - 2>&1 | rg 'max_volume:'; \
      echo 'loudnorm2:'; \
      ffmpeg -nostdin -i '$PARITY_DIR/c_loud/test_clip_converted.mkv' -af loudnorm=I=-11:TP=-1.0:LRA=7:print_format=summary -f null - 2>&1 | rg 'Output Integrated|Output True Peak|Target Offset'; \
    } > '$PARITY_DIR/metrics.txt'"
else
  echo "[WARN] test.mp4 not found, skipping parity test" | tee -a "$SUMMARY"
fi

# -----------------------------
# Final summary
# -----------------------------
{
  echo "Regression run: $TS"
  echo "Root: $ROOT_DIR"
  echo "Results: $RESULTS_DIR"
  echo
  echo "Passed: $PASS_COUNT"
  echo "Failed: $FAIL_COUNT"
  echo
  echo "Status table: $STATUS_TSV"
  echo "Logs: $LOG_DIR"
  if [[ -f "$PARITY_DIR/md5.txt" ]]; then
    echo "Parity MD5: $PARITY_DIR/md5.txt"
  fi
  if [[ -f "$PARITY_DIR/metrics.txt" ]]; then
    echo "Parity metrics: $PARITY_DIR/metrics.txt"
  fi
} > "$SUMMARY"

cat "$SUMMARY"

echo
if [[ $FAIL_COUNT -eq 0 ]]; then
  echo "ALL CHECKS PASSED"
  exit 0
fi

echo "SOME CHECKS FAILED"
exit 1
