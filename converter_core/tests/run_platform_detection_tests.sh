#!/bin/bash
# Tests for converter_core platform detection (macOS/Linux).
# Builds tests/platform_detection_probe.pas into a temporary folder and checks
# tool resolution and validation. The "real tools" cases need
# converter_core/platform/tools (scripts/<os>_prepare.sh); they are reported as
# NOT AVAILABLE when it is missing.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CORE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REAL_TOOLS="$CORE_DIR/platform/tools"
REAL_HQ="$CORE_DIR/platform/hq_converter"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

PASSED=0
FAILED=0
SKIPPED=0

pass() { printf 'PASS  %s\n' "$1"; PASSED=$((PASSED + 1)); }
fail() { printf 'FAIL  %s: %s\n' "$1" "$2"; FAILED=$((FAILED + 1)); }
skip() { printf 'NOT AVAILABLE  %s: %s\n' "$1" "$2"; SKIPPED=$((SKIPPED + 1)); }

value() { printf '%s\n' "$1" | sed -n "s/^$2=//p"; }

# expect NAME OUTPUT KEY OPERATOR EXPECTED   (OPERATOR: eq | contains | absent)
expect() {
  local name="$1" out="$2" key="$3" op="$4" want="$5" got
  got="$(value "$out" "$key")"
  case "$op" in
    eq)       [ "$got" = "$want" ] ;;
    contains) case ",$got," in *",$want,"*) true ;; *) [[ "$got" == *"$want"* ]] ;; esac ;;
    absent)   case ",$got," in *",$want,"*) false ;; *) true ;; esac ;;
  esac || { fail "$name" "$key=$got (expected $op '$want')"; return 1; }
}

# ---------------------------------------------------------------- build

mkdir -p "$WORK/units" "$WORK/app"
if ! fpc -Mobjfpc -Sh -Fu"$CORE_DIR/src" -FU"$WORK/units" -o"$WORK/app/probe" \
     "$SCRIPT_DIR/platform_detection_probe.pas" > "$WORK/build.log" 2>&1; then
  cat "$WORK/build.log"
  echo "FAIL  build"
  exit 1
fi
PROBE="$WORK/app/probe"

# ---------------------------------------------------------------- fake tools

# make_fake_tools DIR [skip-tool] [missing-encoder]
make_fake_tools() {
  local dir="$1" skip_tool="${2:-}" no_encoder="${3:-}" t
  mkdir -p "$dir"
  for t in ffprobe mkvmerge MP4Box; do
    [ "$t" = "$skip_tool" ] && continue
    printf '#!/bin/sh\nexit 0\n' > "$dir/$t"
    chmod +x "$dir/$t"
  done
  [ "$skip_tool" = "ffmpeg" ] && return
  cat > "$dir/ffmpeg" <<EOF
#!/bin/sh
for a in "\$@"; do
  case "\$a" in
    -encoders)
      echo "Encoders:"
      echo " ------"
      for e in prores_ks libx264 libfdk_aac ac3; do
        [ "\$e" = "$no_encoder" ] || echo " V....D \$e    fake \$e"
      done ;;
    -decoders)
      echo " ------"
      echo " V..... libdav1d    fake dav1d" ;;
    -buildconf)
      echo "    --enable-libfdk-aac"
      echo "    --enable-libsoxr" ;;
  esac
done
exit 0
EOF
  chmod +x "$dir/ffmpeg"
}

run_probe() { env "$@" "$PROBE"; }

# ---------------------------------------------------------------- fixture cases

T=fake_tools_allowed
make_fake_tools "$WORK/ok/tools"
OUT="$(run_probe CONVERTER_CORE_TOOLS_DIR="$WORK/ok/tools")"
expect $T "$OUT" ALLOWED eq 1 && expect $T "$OUT" ERROR eq "" \
  && expect $T "$OUT" PLATFORMS contains all \
  && expect $T "$OUT" PLATFORMS absent hq-converter \
  && expect $T "$OUT" MP4BOX eq "$WORK/ok/tools/MP4Box" && pass $T

T=missing_tools_folder
OUT="$(run_probe CONVERTER_CORE_TOOLS_DIR="$WORK/does_not_exist")"
expect $T "$OUT" ALLOWED eq 0 && expect $T "$OUT" ERROR contains "Tools folder not found" \
  && expect $T "$OUT" PLATFORMS eq "" && pass $T

T=missing_tool_no_fallback
make_fake_tools "$WORK/no_mp4box/tools" MP4Box
OUT="$(run_probe CONVERTER_CORE_TOOLS_DIR="$WORK/no_mp4box/tools" PATH="/opt/local/bin:$PATH")"
expect $T "$OUT" ALLOWED eq 0 && expect $T "$OUT" ERROR contains '"MP4Box" not found' \
  && expect $T "$OUT" PLATFORMS eq "" && pass $T

T=tool_not_executable
make_fake_tools "$WORK/no_exec/tools"
chmod -x "$WORK/no_exec/tools/mkvmerge"
OUT="$(run_probe CONVERTER_CORE_TOOLS_DIR="$WORK/no_exec/tools")"
expect $T "$OUT" ALLOWED eq 0 && expect $T "$OUT" ERROR contains '"mkvmerge" is not executable' \
  && pass $T

T=tool_does_not_start
make_fake_tools "$WORK/bad_start/tools"
printf '#!/bin/sh\nexit 3\n' > "$WORK/bad_start/tools/ffprobe"
OUT="$(run_probe CONVERTER_CORE_TOOLS_DIR="$WORK/bad_start/tools")"
expect $T "$OUT" ALLOWED eq 0 && expect $T "$OUT" ERROR contains '"ffprobe" does not start' \
  && pass $T

T=missing_encoder
make_fake_tools "$WORK/no_fdk/tools" "" libfdk_aac
OUT="$(run_probe CONVERTER_CORE_TOOLS_DIR="$WORK/no_fdk/tools")"
expect $T "$OUT" ALLOWED eq 0 && expect $T "$OUT" ERROR contains 'encoder "libfdk_aac" is missing' \
  && pass $T

T=hq_detected_next_to_tools
make_fake_tools "$WORK/hq/tools"
for f in bin/ffmpeg bin/ffprobe bin/mkvmerge \
         modules/encoder_orchestrator_v2/runner/encoder_orchestrator_v2_runner \
         modules/encoder_orchestrator_v2/config/toolchain_runtime.ini \
         modules/encoder_orchestrator_v2/config/presets/encoder_presets_v2.json; do
  mkdir -p "$(dirname "$WORK/hq/hq_converter/$f")"
  : > "$WORK/hq/hq_converter/$f"
done
mkdir -p "$WORK/hq/hq_converter/native/libav-sdk/lib"
OUT="$(run_probe CONVERTER_CORE_TOOLS_DIR="$WORK/hq/tools")"
expect $T "$OUT" ALLOWED eq 1 && expect $T "$OUT" PLATFORMS contains hq-converter && pass $T

T=default_dir_next_to_binary
mkdir -p "$WORK/installed"
cp "$PROBE" "$WORK/installed/probe"
make_fake_tools "$WORK/installed/tools"
OUT="$(env -u CONVERTER_CORE_TOOLS_DIR "$WORK/installed/probe")"
INSTALLED_REAL="$(cd "$WORK/installed" && pwd -P)"
expect $T "$OUT" ALLOWED eq 1 \
  && case "$(value "$OUT" TOOLS_DIR)" in
       "$WORK/installed/tools"|"$INSTALLED_REAL/tools") true ;;
       *) fail $T "TOOLS_DIR=$(value "$OUT" TOOLS_DIR)"; false ;;
     esac && pass $T

# ---------------------------------------------------------------- real tools

T=real_tools
if [ -x "$REAL_TOOLS/ffmpeg" ]; then
  OUT="$(run_probe CONVERTER_CORE_TOOLS_DIR="$REAL_TOOLS")"
  expect $T "$OUT" ALLOWED eq 1 && expect $T "$OUT" ERROR eq "" \
    && expect $T "$OUT" PLATFORMS contains all \
    && expect $T "$OUT" FFMPEG eq "$REAL_TOOLS/ffmpeg" && pass $T
  printf '      %s\n' "$(printf '%s\n' "$OUT" | grep '^PLATFORMS=')"

  T=real_hq_converter
  if [ -d "$REAL_HQ" ]; then
    expect $T "$OUT" PLATFORMS contains hq-converter && pass $T
  else
    skip $T "$REAL_HQ not prepared"
  fi

  T=real_mac_model
  if [ "$(uname -s)" = "Darwin" ]; then
    case "$(sysctl -n hw.model)" in
      MacPro6,1)  expect $T "$OUT" PLATFORMS contains darwin_MacPro61 && pass $T ;;
      iMacPro1,1) expect $T "$OUT" PLATFORMS contains darwin_iMacPro11 && pass $T ;;
      MacPro7,1)  expect $T "$OUT" PLATFORMS contains darwin_MacPro71 && pass $T ;;
      *) expect $T "$OUT" PLATFORMS eq "all$( [ -d "$REAL_HQ" ] && echo ,hq-converter)" && pass $T ;;
    esac
  fi
else
  skip $T "$REAL_TOOLS not prepared (run scripts/<os>_prepare.sh)"
fi

printf '\n%d passed, %d failed, %d not available\n' "$PASSED" "$FAILED" "$SKIPPED"
[ "$FAILED" = 0 ]
