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

BUNDLED_FFMPEG="${BUNDLED_FFMPEG:-}"
if [[ -z "$BUNDLED_FFMPEG" ]]; then
  BUNDLED_FFMPEG="$APP_MACOS_DIR/../Resources/bin/ffmpeg"
fi

if [[ ! -f "$BUNDLED_FFMPEG" ]]; then
  echo "WARNING: ffmpeg binary not found at $BUNDLED_FFMPEG. Skipping ffmpeg dependency bundling."
  exit 0
fi

mkdir -p "$RES_LIB_DIR"

install_name_tool -add_rpath "@executable_path/../lib" "$BUNDLED_FFMPEG" 2>/dev/null || true

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

bundle_dylib_deps "$BUNDLED_FFMPEG"

BUNDLED_FFPROBE="${BUNDLED_FFPROBE:-}"
if [[ -z "$BUNDLED_FFPROBE" ]]; then
  BUNDLED_FFPROBE="$APP_MACOS_DIR/../Resources/bin/ffprobe"
fi

if [[ -f "$BUNDLED_FFPROBE" ]]; then
  install_name_tool -add_rpath "@executable_path/../lib" "$BUNDLED_FFPROBE" 2>/dev/null || true
  bundle_dylib_deps "$BUNDLED_FFPROBE"
fi

echo "Bundled ffmpeg/ffprobe deps: $BUNDLED_FFMPEG (dylibs=${#_BUNDLED_DYLIBS[@]})"
