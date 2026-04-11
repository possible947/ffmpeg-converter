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

SYSTEM_MKVMERGE="${SYSTEM_MKVMERGE:-}"
if [[ -z "$SYSTEM_MKVMERGE" ]]; then
  for p in /opt/local/bin/mkvmerge /opt/homebrew/bin/mkvmerge /usr/local/bin/mkvmerge; do
    if [[ -x "$p" ]]; then
      SYSTEM_MKVMERGE="$p"
      break
    fi
  done
fi

if [[ -z "$SYSTEM_MKVMERGE" || ! -x "$SYSTEM_MKVMERGE" ]]; then
  echo "WARNING: mkvmerge not found (tried SYSTEM_MKVMERGE and common paths). Mux mode will require PATH/runtime mkvmerge."
  exit 0
fi

mkdir -p "$RES_LIB_DIR"
cp "$SYSTEM_MKVMERGE" "$RES_BIN_DIR/mkvmerge"
chmod +x "$RES_BIN_DIR/mkvmerge"

install_name_tool -add_rpath "@executable_path/../lib" "$RES_BIN_DIR/mkvmerge" 2>/dev/null || true

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

bundle_dylib_deps "$RES_BIN_DIR/mkvmerge"

echo "Bundled mkvmerge: $RES_BIN_DIR/mkvmerge (dylibs=${#_BUNDLED_DYLIBS[@]})"
