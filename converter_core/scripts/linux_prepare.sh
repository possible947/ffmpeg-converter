#!/bin/bash
# Prepares converter_core/platform/{tools,hq_converter} on Linux.
#
#   tools/         ffmpeg, ffprobe from src/platform/linux/bin, mkvmerge and MP4Box
#                  from the system; their shared libraries go to tools/lib/
#   hq_converter/  complete copy of third_party/hq_converter
#
# Every ELF file is made self-contained: shared libraries that are not part of
# the base system are copied to <root>/lib and found through an $ORIGIN-relative
# RUNPATH (patchelf). A library that already sits next to the referencing file
# inside hq_converter/ is reused. See converter_core/docs/Developer_Descriptions.md.
#
# Not bundled (provided by the host): the C runtime/loader (glibc, libgcc_s,
# libstdc++) and GPU/driver-facing libraries (libva, libvulkan, libdrm, libGL,
# libEGL, libcuda, libnvidia-*, libOpenCL).
#
# Overrides (environment):
#   FFMPEG_SRC_DIR   default: <repo>/src/platform/linux/bin
#   HQ_SRC_DIR       default: <repo>/third_party/hq_converter
#   MKVMERGE_SRC     default: first mkvmerge in PATH
#   MP4BOX_SRC       default: first MP4Box in PATH
#   OUTPUT_DIR       default: <repo>/converter_core/platform

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

FFMPEG_SRC_DIR="${FFMPEG_SRC_DIR:-$REPO_ROOT/src/platform/linux/bin}"
HQ_SRC_DIR="${HQ_SRC_DIR:-$REPO_ROOT/third_party/hq_converter}"
MKVMERGE_SRC="${MKVMERGE_SRC:-$(command -v mkvmerge || true)}"
MP4BOX_SRC="${MP4BOX_SRC:-$(command -v MP4Box || true)}"
OUTPUT_DIR="${OUTPUT_DIR:-$REPO_ROOT/converter_core/platform}"

STAGING_DIR="$OUTPUT_DIR/.staging"

log()  { printf '==> %s\n' "$*"; }
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- prerequisites

[ "$(uname -s)" = "Linux" ] || fail "linux_prepare.sh must run on Linux"
for cmd in patchelf ldd file readelf; do
  command -v "$cmd" >/dev/null 2>&1 || fail "required command not found: $cmd"
done

for f in "$FFMPEG_SRC_DIR/ffmpeg" "$FFMPEG_SRC_DIR/ffprobe" "$MKVMERGE_SRC" "$MP4BOX_SRC"; do
  [ -n "$f" ] && [ -x "$f" ] || fail "source tool not found or not executable: ${f:-<unset>}"
done
[ -d "$HQ_SRC_DIR/bin" ] || fail "HQ_converter release not found: $HQ_SRC_DIR"

# ---------------------------------------------------------------- ELF helpers

# Prints all ELF regular files below $1 (symlinks are skipped).
list_elf() {
  find "$1" -type f -print0 \
    | xargs -0 file --mime-type -- 2>/dev/null \
    | awk -F': ' '$2 ~ /application\/(x-executable|x-pie-executable|x-sharedlib)/ { print $1 }'
}

# True for libraries that stay provided by the host.
is_system_lib() {
  case "$(basename "$1")" in
    linux-vdso.so*|ld-linux*|libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|\
    libutil.so*|libresolv.so*|libmvec.so*|libnsl.so*|libanl.so*|libgcc_s.so*|\
    libstdc++.so*|libva.so*|libva-*.so*|libvulkan.so*|libdrm*.so*|libGL.so*|\
    libGLX*.so*|libGLdispatch.so*|libEGL*.so*|libcuda.so*|libnvidia-*.so*|\
    libOpenCL.so*) return 0 ;;
  esac
  return 1
}

# "name path" pairs of the direct dependencies (DT_NEEDED) of $1; $2 = extra
# library path. Transitive dependencies of host-provided libraries (for example
# X11 behind libva) are deliberately not listed.
resolve_deps() {
  local needed
  needed="$(readelf -d "$1" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')"
  [ -n "$needed" ] || return 0
  LD_LIBRARY_PATH="$2" ldd "$1" 2>/dev/null \
    | awk -v needed="$needed" '
        BEGIN { n = split(needed, a, "\n"); for (i = 1; i <= n; i++) want[a[i]] = 1 }
        ($1 in want) && /=> \// { print $1, $3 }
        ($1 in want) && /=> not found/ { print $1, "NOTFOUND" }'
}

# Existing $ORIGIN-based RPATH/RUNPATH entries of $1, one per line.
origin_rpaths() {
  patchelf --print-rpath "$1" 2>/dev/null | tr ':' '\n' | grep -F '$ORIGIN' || true
}

# Relative $ORIGIN path from directory of file $1 to <root>/lib ($2 = root).
lib_origin() {
  local rel
  rel="$(realpath --relative-to="$(dirname "$1")" "$2/lib")"
  if [ "$rel" = "." ]; then printf '$ORIGIN\n'; else printf '$ORIGIN/%s\n' "$rel"; fi
}

# Copies libraries of every ELF file below $1 that are not system libraries and
# not already next to the file, until no new library appears.
bundle_tree() {
  local root="$1" reuse="$2" pass=0 added f name path libpath
  while :; do
    pass=$((pass + 1))
    [ "$pass" -le 20 ] || fail "bundling did not converge in $root"
    added=0
    for f in $(list_elf "$root"); do
      libpath="$(dirname "$f"):$root/lib"
      while read -r name path; do
        [ -n "$name" ] || continue
        is_system_lib "$name" && continue
        [ "$path" != "NOTFOUND" ] || fail "$f needs missing library $name"
        case "$path" in "$root"/*) continue ;; esac
        [ "$reuse" = "reuse" ] && [ -e "$(dirname "$f")/$name" ] && continue
        mkdir -p "$root/lib"
        cp -L "$path" "$root/lib/$name"
        chmod u+w "$root/lib/$name"
        added=1
      done < <(resolve_deps "$f" "$libpath")
    done
    [ "$added" = 1 ] || break
  done
}

# Sets an $ORIGIN-relative RUNPATH on every ELF file below $1.
set_rpaths() {
  local root="$1" f rp
  for f in $(list_elf "$root"); do
    chmod u+w "$f"
    rp="$( { origin_rpaths "$f"; printf '$ORIGIN\n'; \
             [ -d "$root/lib" ] && lib_origin "$f" "$root"; } | awk '!seen[$0]++' | paste -sd: -)"
    patchelf --set-rpath "$rp" "$f"
  done
}

# ---------------------------------------------------------------- verification

# Fails if an ELF file below $1 depends on a non-system library outside $1,
# resolving with an empty LD_LIBRARY_PATH.
verify_tree() {
  local root="$1" f name path bad=0
  for f in $(list_elf "$root"); do
    while read -r name path; do
      [ -n "$name" ] || continue
      is_system_lib "$name" && continue
      case "$path" in
        "$root"/*) ;;
        *) printf 'external reference: %s -> %s (%s)\n' "${f#"$STAGING_DIR"/}" "$name" "$path" >&2
           bad=1 ;;
      esac
    done < <(resolve_deps "$f" "")
  done
  [ "$bad" = 0 ] || fail "self-containment check failed in $root"
}

verify_runs() {
  local tool="$1"
  shift
  env -u LD_LIBRARY_PATH "$tool" "$@" >/dev/null 2>&1 || fail "tool does not start: $tool $*"
}

# ---------------------------------------------------------------- build

rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR/tools"

log "Copying ffmpeg/ffprobe from $FFMPEG_SRC_DIR"
cp -p "$FFMPEG_SRC_DIR/ffmpeg" "$FFMPEG_SRC_DIR/ffprobe" "$STAGING_DIR/tools/"
for lib in "$FFMPEG_SRC_DIR"/*.so*; do
  [ -e "$lib" ] && cp -pP "$lib" "$STAGING_DIR/tools/"
done

log "Copying mkvmerge/MP4Box ($MKVMERGE_SRC, $MP4BOX_SRC)"
cp -L "$MKVMERGE_SRC" "$STAGING_DIR/tools/mkvmerge"
cp -L "$MP4BOX_SRC" "$STAGING_DIR/tools/MP4Box"

log "Bundling tools"
bundle_tree "$STAGING_DIR/tools" "noreuse"
set_rpaths "$STAGING_DIR/tools"

log "Copying HQ_converter from $HQ_SRC_DIR"
cp -a "$HQ_SRC_DIR" "$STAGING_DIR/hq_converter"

log "Bundling HQ_converter"
bundle_tree "$STAGING_DIR/hq_converter" "reuse"
set_rpaths "$STAGING_DIR/hq_converter"

log "Verifying self-containment"
verify_tree "$STAGING_DIR/tools"
verify_tree "$STAGING_DIR/hq_converter"

log "Verifying that tools start without host libraries"
verify_runs "$STAGING_DIR/tools/ffmpeg" -hide_banner -version
verify_runs "$STAGING_DIR/tools/ffprobe" -hide_banner -version
verify_runs "$STAGING_DIR/tools/mkvmerge" --version
verify_runs "$STAGING_DIR/tools/MP4Box" -version
verify_runs "$STAGING_DIR/hq_converter/bin/ffmpeg" -hide_banner -version
verify_runs "$STAGING_DIR/hq_converter/bin/ffprobe" -hide_banner -version
verify_runs "$STAGING_DIR/hq_converter/bin/mkvmerge" --version

rm -rf "$OUTPUT_DIR/tools" "$OUTPUT_DIR/hq_converter"
mv "$STAGING_DIR/tools" "$OUTPUT_DIR/tools"
mv "$STAGING_DIR/hq_converter" "$OUTPUT_DIR/hq_converter"
rmdir "$STAGING_DIR"

log "Done: $OUTPUT_DIR/tools, $OUTPUT_DIR/hq_converter"
