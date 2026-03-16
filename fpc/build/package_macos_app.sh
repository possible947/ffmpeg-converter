#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP_NAME="form.app"
APP_DIR="$ROOT/fpc/gui/$APP_NAME"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
RES_DIR="$CONTENTS_DIR/Resources"
RES_BIN_DIR="$RES_DIR/bin"
SRC_BIN="${1:-$ROOT/fpc/gui/ffmpeg_converter_gui}"
DST_BIN="$MACOS_DIR/ffmpeg_converter_gui"
PROJECT_FFMPEG="$ROOT/ffmpeg"
PROJECT_FFPROBE="$ROOT/ffprobe"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "Skipping macOS app packaging on non-Darwin host."
  exit 0
fi

if [[ ! -f "$SRC_BIN" ]]; then
  echo "ERROR: source GUI binary not found: $SRC_BIN"
  exit 1
fi

mkdir -p "$MACOS_DIR" "$RES_DIR" "$RES_BIN_DIR"

# Remove stale Lazarus symlinked launchers so the bundle is self-contained.
find "$MACOS_DIR" -maxdepth 1 -type l -delete

cat > "$CONTENTS_DIR/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>
  <string>ffmpeg_converter</string>
  <key>CFBundleDisplayName</key>
  <string>ffmpeg_converter</string>
  <key>CFBundleIdentifier</key>
  <string>local.ffmpeg.converter</string>
  <key>CFBundleVersion</key>
  <string>2.0.0</string>
  <key>CFBundleShortVersionString</key>
  <string>2.0.0</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleExecutable</key>
  <string>ffmpeg_converter_gui</string>
</dict>
</plist>
PLIST

rm -f "$DST_BIN"
cp "$SRC_BIN" "$DST_BIN"
chmod +x "$DST_BIN"

if [[ -L "$DST_BIN" ]]; then
  echo "ERROR: packaged app binary is a symlink: $DST_BIN"
  exit 1
fi

if [[ -f "$PROJECT_FFMPEG" && -f "$PROJECT_FFPROBE" ]]; then
  cp "$PROJECT_FFMPEG" "$RES_BIN_DIR/ffmpeg"
  cp "$PROJECT_FFPROBE" "$RES_BIN_DIR/ffprobe"
  chmod +x "$RES_BIN_DIR/ffmpeg" "$RES_BIN_DIR/ffprobe"
  echo "Bundled tools: $RES_BIN_DIR/ffmpeg"
  echo "Bundled tools: $RES_BIN_DIR/ffprobe"
else
  echo "WARNING: ffmpeg/ffprobe not found in project root; app may depend on PATH."
fi

# Bundle MP4Box (GPAC) from system and all its non-system dylib dependencies.
# Required by the Apple M4V creation module (apple_m4v_creator).
SYSTEM_MP4BOX="${SYSTEM_MP4BOX:-/opt/local/bin/MP4Box}"
RES_LIB_DIR="$RES_DIR/lib"

# Recursively collect non-system dylibs (paths under /opt/local or /usr/local/opt)
# and patch rpath in the copied binaries so they resolve to @rpath/../lib/.
_BUNDLED_DYLIBS=()
_bundle_dylib_deps() {
  local binary="$1"
  local lib_dir="$2"
  local srclib libname dstlib

  while IFS= read -r srclib; do
    libname="$(basename "$srclib")"
    dstlib="$lib_dir/$libname"

    # Patch the reference in the caller binary
    install_name_tool -change "$srclib" "@rpath/$libname" "$binary" 2>/dev/null

    # Skip already-processed libs to avoid cycles
    local already=0
    for l in "${_BUNDLED_DYLIBS[@]}"; do
      [[ "$l" == "$libname" ]] && already=1 && break
    done
    [[ "$already" -eq 1 ]] && continue
    _BUNDLED_DYLIBS+=("$libname")

    if [[ -f "$srclib" ]]; then
      cp "$srclib" "$dstlib"
      chmod +w "$dstlib"
      # Update the dylib's own install name
      install_name_tool -id "@rpath/$libname" "$dstlib" 2>/dev/null
      # Recurse
      _bundle_dylib_deps "$dstlib" "$lib_dir"
    fi
  done < <(otool -L "$binary" 2>/dev/null | awk 'NR>1 {print $1}' \
    | grep -E '^(/opt/local|/usr/local/opt|/opt/homebrew/opt)')
}

if [[ -f "$SYSTEM_MP4BOX" ]]; then
  mkdir -p "$RES_LIB_DIR"
  cp "$SYSTEM_MP4BOX" "$RES_BIN_DIR/MP4Box"
  chmod +x "$RES_BIN_DIR/MP4Box"
  # Add rpath pointing to ../lib so @rpath dylibs resolve correctly
  install_name_tool -add_rpath "@executable_path/../lib" "$RES_BIN_DIR/MP4Box" 2>/dev/null
  _bundle_dylib_deps "$RES_BIN_DIR/MP4Box" "$RES_LIB_DIR"
  # Make bundled dylibs find their own siblings via @rpath
  for dstlib in "$RES_LIB_DIR"/*.dylib; do
    install_name_tool -add_rpath "@loader_path" "$dstlib" 2>/dev/null
  done
  echo "Bundled tools: $RES_BIN_DIR/MP4Box (with ${#_BUNDLED_DYLIBS[@]} dylib(s))"
else
  echo "WARNING: MP4Box not found at $SYSTEM_MP4BOX; Apple M4V creation will fail."
  echo "         Install GPAC: sudo port install gpac"
fi

echo "Packaged app: $APP_DIR"
echo "Bundle executable: $DST_BIN"
