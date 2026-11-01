#!/bin/bash
# Prepares converter_core/platform/{tools,hq_converter} on macOS.
#
#   tools/         ffmpeg, ffprobe (+ their libraries, flat) from src/platform/macos/bin,
#                  mkvmerge, MP4Box from the system; their libraries go to tools/lib/
#   hq_converter/  complete copy of third_party/hq_converter
#
# Every Mach-O file is made self-contained: non-system libraries are copied and
# referenced through @rpath, foreign LC_RPATH entries are removed and changed files
# are ad-hoc re-signed. See converter_core/docs/Developer_Descriptions.md.
#
# Overrides (environment):
#   FFMPEG_SRC_DIR   default: <repo>/src/platform/macos/bin
#   HQ_SRC_DIR       default: <repo>/third_party/hq_converter
#   SYSTEM_PREFIX    default: /opt/local (MacPorts)
#   OUTPUT_DIR       default: <repo>/converter_core/platform

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

FFMPEG_SRC_DIR="${FFMPEG_SRC_DIR:-$REPO_ROOT/src/platform/macos/bin}"
HQ_SRC_DIR="${HQ_SRC_DIR:-$REPO_ROOT/third_party/hq_converter}"
SYSTEM_PREFIX="${SYSTEM_PREFIX:-/opt/local}"
OUTPUT_DIR="${OUTPUT_DIR:-$REPO_ROOT/converter_core/platform}"

STAGING_DIR="$OUTPUT_DIR/.staging"
MODIFIED_LIST="$STAGING_DIR/.modified"

log()  { printf '==> %s\n' "$*"; }
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- prerequisites

[ "$(uname -s)" = "Darwin" ] || fail "macos_prepare.sh must run on macOS"
for cmd in otool install_name_tool codesign file ditto; do
  command -v "$cmd" >/dev/null 2>&1 || fail "required command not found: $cmd"
done

MKVMERGE_SRC="$SYSTEM_PREFIX/bin/mkvmerge"
MP4BOX_SRC="$SYSTEM_PREFIX/bin/MP4Box"

for f in "$FFMPEG_SRC_DIR/ffmpeg" "$FFMPEG_SRC_DIR/ffprobe" "$MKVMERGE_SRC" "$MP4BOX_SRC"; do
  [ -x "$f" ] || fail "source tool not found or not executable: $f"
done
[ -d "$HQ_SRC_DIR/bin" ] || fail "HQ_converter release not found: $HQ_SRC_DIR"

# ---------------------------------------------------------------- Mach-O helpers

# Prints all Mach-O regular files below $1 (symlinks are skipped).
list_macho() {
  find "$1" -type f -print0 \
    | xargs -0 file --mime-type -- 2>/dev/null \
    | awk -F': ' '$2 ~ /application\/x-mach-binary/ { print $1 }'
}

# A path is "foreign" when it is absolute and not provided by macOS itself.
is_foreign() {
  case "$1" in
    /usr/lib/*|/System/*) return 1 ;;
    /*) return 0 ;;
    *) return 1 ;;
  esac
}

dylib_id() { otool -D "$1" | tail -n +2; }

# Dependencies of $1, without its own install name.
macho_deps() {
  local id
  id="$(dylib_id "$1")"
  otool -L "$1" | tail -n +2 | awk '{ print $1 }' | while read -r dep; do
    [ "$dep" = "$id" ] && continue
    printf '%s\n' "$dep"
  done
}

macho_rpaths() {
  otool -l "$1" | awk '/cmd LC_RPATH/ { getline; getline; print $2 }'
}

# Captured first: with pipefail, "otool | grep -q" can fail on SIGPIPE.
has_rpath() {
  local rpaths nl=$'\n'
  rpaths="$(macho_rpaths "$1")"
  case "$nl$rpaths$nl" in
    *"$nl$2$nl"*) return 0 ;;
  esac
  return 1
}

mark_modified() { printf '%s\n' "$1" >> "$MODIFIED_LIST"; }

# install_name_tool, quiet on success (it always warns about invalidated signatures).
int() {
  local out
  out="$(install_name_tool "$@" 2>&1)" || fail "install_name_tool $*: $out"
}

# @loader_path-based rpath from the directory of file $1 to directory $2,
# where $2 is either that directory or <root>/lib and $3 is <root>.
loader_rpath() {
  local file_dir target_dir root rel up
  file_dir="$(dirname "$1")"; target_dir="$2"; root="$3"
  if [ "$file_dir" = "$target_dir" ]; then
    printf '@loader_path\n'; return
  fi
  rel="${file_dir#"$root"}"; rel="${rel#/}"
  up=""
  if [ -n "$rel" ]; then
    up="$(printf '%s' "$rel" | awk -F/ '{ for (i = 1; i <= NF; i++) printf "../" }')"
  fi
  printf '@loader_path/%s%s\n' "$up" "${target_dir#"$root"/}"
}

# Makes one Mach-O file independent of foreign libraries.
#   $1 file, $2 root of the set, $3 "reuse" to prefer a same-named sibling library
# Copies every newly needed library into <root>/lib (processed in later passes).
bundle_file() {
  local file="$1" root="$2" reuse="$3" libdir="$2/lib"
  local changed=0 dep name target_dir rp id

  chmod u+w "$file"

  for dep in $(macho_deps "$file"); do
    is_foreign "$dep" || continue
    name="$(basename "$dep")"
    if [ "$reuse" = "reuse" ] && [ -f "$(dirname "$file")/$name" ] \
       && [ "$(dirname "$file")/$name" != "$file" ]; then
      target_dir="$(dirname "$file")"
    else
      target_dir="$libdir"
      if [ ! -e "$libdir/$name" ]; then
        [ -f "$dep" ] || fail "$file needs missing library $dep"
        mkdir -p "$libdir"
        cp -L "$dep" "$libdir/$name"
        chmod u+w "$libdir/$name"
      fi
    fi
    int -change "$dep" "@rpath/$name" "$file"
    rp="$(loader_rpath "$file" "$target_dir" "$root")"
    has_rpath "$file" "$rp" || int -add_rpath "$rp" "$file"
    changed=1
  done

  for rp in $(macho_rpaths "$file"); do
    is_foreign "$rp" || continue
    while has_rpath "$file" "$rp"; do
      int -delete_rpath "$rp" "$file"
    done
    changed=1
  done

  id="$(dylib_id "$file")"
  if [ -n "$id" ] && is_foreign "$id"; then
    int -id "@rpath/$(basename "$file")" "$file"
    changed=1
  fi

  [ "$changed" = 1 ] && mark_modified "$file"
  return 0
}

needs_bundling() {
  local x
  for x in $(macho_deps "$1") $(macho_rpaths "$1") $(dylib_id "$1"); do
    is_foreign "$x" && return 0
  done
  return 1
}

# Repeats until no Mach-O file below $1 references foreign paths.
bundle_tree() {
  local root="$1" reuse="$2" pass=0 pending f
  while :; do
    pass=$((pass + 1))
    [ "$pass" -le 20 ] || fail "bundling did not converge in $root"
    pending=0
    for f in $(list_macho "$root"); do
      if needs_bundling "$f"; then
        bundle_file "$f" "$root" "$reuse"
        pending=1
      fi
    done
    [ "$pending" = 1 ] || break
  done
}

resign_modified() {
  [ -s "$MODIFIED_LIST" ] || return 0
  sort -u "$MODIFIED_LIST" | while read -r f; do
    codesign --force --sign - "$f" >/dev/null 2>&1 \
      || codesign --force --sign - --preserve-metadata=identifier "$f" >/dev/null \
      || fail "codesign failed: $f"
  done
}

# ---------------------------------------------------------------- verification

verify_tree() {
  local root="$1" f x bad=0
  for f in $(list_macho "$root"); do
    for x in $(macho_deps "$f") $(macho_rpaths "$f") $(dylib_id "$f"); do
      if is_foreign "$x"; then
        printf 'foreign reference: %s -> %s\n' "${f#"$STAGING_DIR"/}" "$x" >&2
        bad=1
      fi
    done
    codesign --verify "$f" >/dev/null 2>&1 \
      || { printf 'invalid signature: %s\n' "${f#"$STAGING_DIR"/}" >&2; bad=1; }
  done
  [ "$bad" = 0 ] || fail "self-containment check failed in $root"
}

# Runs a tool and fails if it does not start or loads a library from SYSTEM_PREFIX.
verify_runs() {
  local tool="$1" out loaded
  shift
  out="$(DYLD_PRINT_LIBRARIES=1 "$tool" "$@" 2>&1)" \
    || fail "tool does not start: $tool $*"
  loaded="$(printf '%s\n' "$out" | grep -F "dyld" | grep -F "$SYSTEM_PREFIX/" || true)"
  [ -z "$loaded" ] || fail "$tool loads system libraries: $(printf '%s' "$loaded" | head -1)"
}

# ---------------------------------------------------------------- build

rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR/tools"
: > "$MODIFIED_LIST"

log "Copying ffmpeg/ffprobe from $FFMPEG_SRC_DIR"
cp -p "$FFMPEG_SRC_DIR/ffmpeg" "$FFMPEG_SRC_DIR/ffprobe" "$STAGING_DIR/tools/"
for lib in "$FFMPEG_SRC_DIR"/*.dylib; do
  [ -e "$lib" ] && cp -p "$lib" "$STAGING_DIR/tools/"
done

log "Copying mkvmerge/MP4Box from $SYSTEM_PREFIX/bin"
cp -L "$MKVMERGE_SRC" "$STAGING_DIR/tools/mkvmerge"
cp -L "$MP4BOX_SRC" "$STAGING_DIR/tools/MP4Box"

log "Bundling tools (this copies the libraries mkvmerge/MP4Box need)"
bundle_tree "$STAGING_DIR/tools" "noreuse"

log "Copying HQ_converter from $HQ_SRC_DIR"
ditto "$HQ_SRC_DIR" "$STAGING_DIR/hq_converter"

log "Bundling HQ_converter"
bundle_tree "$STAGING_DIR/hq_converter" "reuse"

log "Re-signing modified files"
resign_modified

log "Verifying self-containment"
verify_tree "$STAGING_DIR/tools"
verify_tree "$STAGING_DIR/hq_converter"

log "Verifying that tools start without $SYSTEM_PREFIX"
verify_runs "$STAGING_DIR/tools/ffmpeg" -hide_banner -version
verify_runs "$STAGING_DIR/tools/ffprobe" -hide_banner -version
verify_runs "$STAGING_DIR/tools/mkvmerge" --version
verify_runs "$STAGING_DIR/tools/MP4Box" -version
verify_runs "$STAGING_DIR/hq_converter/bin/ffmpeg" -hide_banner -version
verify_runs "$STAGING_DIR/hq_converter/bin/ffprobe" -hide_banner -version
verify_runs "$STAGING_DIR/hq_converter/bin/mkvmerge" --version

rm -f "$MODIFIED_LIST"
rm -rf "$OUTPUT_DIR/tools" "$OUTPUT_DIR/hq_converter"
mv "$STAGING_DIR/tools" "$OUTPUT_DIR/tools"
mv "$STAGING_DIR/hq_converter" "$OUTPUT_DIR/hq_converter"
rmdir "$STAGING_DIR"

log "Done: $OUTPUT_DIR/tools, $OUTPUT_DIR/hq_converter"
