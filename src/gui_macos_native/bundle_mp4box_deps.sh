#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <app_macos_dir>"
  exit 2
fi

APP_MACOS_DIR="$1"
RES_DIR="$(cd "$APP_MACOS_DIR/../Resources" && pwd)"
RES_BIN_DIR="$RES_DIR/bin"
RES_LIB_DIR="$RES_DIR/lib"

mkdir -p "$RES_BIN_DIR"

SYSTEM_MP4BOX="${SYSTEM_MP4BOX:-}"
if [[ -z "$SYSTEM_MP4BOX" ]]; then
  for p in /opt/local/bin/MP4Box /opt/homebrew/bin/MP4Box /usr/local/bin/MP4Box; do
    if [[ -x "$p" ]]; then
      SYSTEM_MP4BOX="$p"
      break
    fi
  done
fi

if [[ -z "$SYSTEM_MP4BOX" || ! -x "$SYSTEM_MP4BOX" ]]; then
  echo "WARNING: MP4Box not found (tried SYSTEM_MP4BOX and common paths). Apple M4V creation will require PATH/runtime MP4Box."
  exit 0
fi

mkdir -p "$RES_LIB_DIR"
cp "$SYSTEM_MP4BOX" "$RES_BIN_DIR/MP4Box"
chmod +x "$RES_BIN_DIR/MP4Box"

install_name_tool -add_rpath "@executable_path/../lib" "$RES_BIN_DIR/MP4Box" 2>/dev/null || true

declare -a _BUNDLED_DYLIBS=()

contains_lib() {
  local libname="$1"
  local existing
  for existing in "${_BUNDLED_DYLIBS[@]:-}"; do
    if [[ "$existing" == "$libname" ]]; then
      return 0
    fi
  done
  return 1
}

bundle_dylib_deps() {
  local binary="$1"
  local srclib
  local libname
  local dstlib

  while IFS= read -r srclib; do
    libname="$(basename "$srclib")"
    dstlib="$RES_LIB_DIR/$libname"

    install_name_tool -change "$srclib" "@rpath/$libname" "$binary" 2>/dev/null || true

    if contains_lib "$libname"; then
      continue
    fi
    _BUNDLED_DYLIBS+=("$libname")

    if [[ -f "$srclib" ]]; then
      cp "$srclib" "$dstlib"
      chmod +w "$dstlib"
      install_name_tool -id "@rpath/$libname" "$dstlib" 2>/dev/null || true
      install_name_tool -add_rpath "@loader_path" "$dstlib" 2>/dev/null || true
      bundle_dylib_deps "$dstlib"
    fi
  done < <(otool -L "$binary" 2>/dev/null | awk 'NR>1 {print $1}' | grep -E '^(/opt/local|/usr/local/opt|/opt/homebrew/opt)' || true)
}

bundle_dylib_deps "$RES_BIN_DIR/MP4Box"

echo "Bundled MP4Box: $RES_BIN_DIR/MP4Box (dylibs=${#_BUNDLED_DYLIBS[@]})"
