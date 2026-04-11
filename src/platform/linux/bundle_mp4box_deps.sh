#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <target_dir>"
  exit 2
fi

TARGET_DIR="$1"
BIN_DIR="$TARGET_DIR/bin"
LIB_DIR="$TARGET_DIR/lib"

mkdir -p "$BIN_DIR" "$LIB_DIR"

SYSTEM_MP4BOX="${SYSTEM_MP4BOX:-}"
if [[ -z "$SYSTEM_MP4BOX" ]]; then
  if command -v MP4Box >/dev/null 2>&1; then
    SYSTEM_MP4BOX="$(command -v MP4Box)"
  else
    for p in /usr/bin/MP4Box /usr/local/bin/MP4Box /opt/local/bin/MP4Box; do
      if [[ -x "$p" ]]; then
        SYSTEM_MP4BOX="$p"
        break
      fi
    done
  fi
fi

if [[ -z "$SYSTEM_MP4BOX" || ! -x "$SYSTEM_MP4BOX" ]]; then
  echo "WARNING: MP4Box not found. Linux build will rely on runtime PATH or MP4BOX_BIN."
  exit 0
fi

TARGET_MP4BOX="$BIN_DIR/MP4Box"
cp "$SYSTEM_MP4BOX" "$TARGET_MP4BOX"
chmod +x "$TARGET_MP4BOX"

declare -A SEEN=()

should_skip_lib() {
  local libname="$1"
  case "$libname" in
    linux-vdso.so.*|ld-linux*.so*|libc.so.*|libm.so.*|libpthread.so.*|librt.so.*|libdl.so.*|libresolv.so.*|libnsl.so.*|libutil.so.*|libanl.so.*)
      return 0
      ;;
  esac
  return 1
}

bundle_deps() {
  local binary="$1"
  local dep
  local dep_path
  local dep_name
  local dst_path

  while IFS= read -r dep; do
    dep_path="${dep%% *}"
    [[ -z "$dep_path" || ! -f "$dep_path" ]] && continue

    dep_name="$(basename "$dep_path")"
    if should_skip_lib "$dep_name"; then
      continue
    fi
    if [[ -n "${SEEN[$dep_name]:-}" ]]; then
      continue
    fi

    SEEN["$dep_name"]=1
    dst_path="$LIB_DIR/$dep_name"
    cp "$dep_path" "$dst_path"
    chmod u+w "$dst_path"
    bundle_deps "$dst_path"
  done < <(ldd "$binary" 2>/dev/null | awk '/=> \/|^\// { for (i = 1; i <= NF; ++i) if ($i ~ /^\//) { print $i; break; } }')

  if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN/../lib:$ORIGIN' "$binary" >/dev/null 2>&1 || true
  fi
}

bundle_deps "$TARGET_MP4BOX"

echo "Bundled MP4Box: $TARGET_MP4BOX (shared-libs=${#SEEN[@]})"