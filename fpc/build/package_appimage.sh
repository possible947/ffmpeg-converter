#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BIN_DIR="${REPO_ROOT}/fpc/bin"
GUI_BIN="${BIN_DIR}/ffmpeg_converter_gui"
PROJECT_TOOLS_DIR="${REPO_ROOT}/src/platform/linux/bin"
BUILD_BIN_DIR="${REPO_ROOT}/build/bin"
OUTPUT_DIR="${1:-${BIN_DIR}}"
APPDIR="${SCRIPT_DIR}/AppDir-fpc"
APPIMAGE_PATH="${OUTPUT_DIR}/ffmpeg_converter_gui_fpc-$(uname -m).AppImage"

mkdir -p "${OUTPUT_DIR}"

echo "=== FPC AppImage Builder ==="
echo "Repo: ${REPO_ROOT}"
echo "Output: ${APPIMAGE_PATH}"

if [ ! -x "${GUI_BIN}" ]; then
  echo "ERROR: GUI binary not found: ${GUI_BIN}"
  echo "Build it first: make -C fpc/build gui"
  exit 1
fi

if [ ! -x "${PROJECT_TOOLS_DIR}/ffmpeg" ] || [ ! -x "${PROJECT_TOOLS_DIR}/ffprobe" ]; then
  echo "ERROR: Missing required project tools in ${PROJECT_TOOLS_DIR}"
  echo "Required: ffmpeg and ffprobe"
  exit 1
fi

APPIMAGETOOL_BIN=""
if command -v appimagetool >/dev/null 2>&1; then
  APPIMAGETOOL_BIN="$(command -v appimagetool)"
elif command -v appimagetool.AppImage >/dev/null 2>&1; then
  APPIMAGETOOL_BIN="$(command -v appimagetool.AppImage)"
else
  echo "ERROR: appimagetool/appimagetool.AppImage not found in PATH"
  exit 1
fi

rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin" "${APPDIR}/usr/lib" "${APPDIR}/usr/share/applications" \
  "${APPDIR}/usr/share/ffmpeg_converter" \
  "${APPDIR}/usr/share/icons/hicolor/256x256/apps" \
  "${APPDIR}/usr/share/icons/hicolor/1024x1024/apps"

cp "${GUI_BIN}" "${APPDIR}/usr/bin/ffmpeg_converter_gui"
chmod +x "${APPDIR}/usr/bin/ffmpeg_converter_gui"

if [ -f "${BIN_DIR}/presets.json" ]; then
  cp "${BIN_DIR}/presets.json" "${APPDIR}/usr/share/ffmpeg_converter/presets.json"
elif [ -f "${REPO_ROOT}/presets.json" ]; then
  cp "${REPO_ROOT}/presets.json" "${APPDIR}/usr/share/ffmpeg_converter/presets.json"
else
  echo "ERROR: presets.json not found"
  exit 1
fi

cp "${PROJECT_TOOLS_DIR}/ffmpeg" "${APPDIR}/usr/bin/ffmpeg"
cp "${PROJECT_TOOLS_DIR}/ffprobe" "${APPDIR}/usr/bin/ffprobe"
chmod +x "${APPDIR}/usr/bin/ffmpeg" "${APPDIR}/usr/bin/ffprobe"

for tool in mkvmerge MP4Box; do
  if [ -x "${PROJECT_TOOLS_DIR}/${tool}" ]; then
    cp "${PROJECT_TOOLS_DIR}/${tool}" "${APPDIR}/usr/bin/${tool}"
    chmod +x "${APPDIR}/usr/bin/${tool}"
    echo "Bundled ${tool} from project"
  elif [ -x "${BUILD_BIN_DIR}/${tool}" ]; then
    cp "${BUILD_BIN_DIR}/${tool}" "${APPDIR}/usr/bin/${tool}"
    chmod +x "${APPDIR}/usr/bin/${tool}"
    echo "Bundled ${tool} from build/bin"
  elif command -v "${tool}" >/dev/null 2>&1; then
    cp "$(command -v "${tool}")" "${APPDIR}/usr/bin/${tool}"
    chmod +x "${APPDIR}/usr/bin/${tool}"
    echo "Bundled ${tool} from system"
  else
    echo "Optional tool ${tool} not found, skipping"
  fi
done

declare -A LIBS_TO_COPY=()

add_binary_deps() {
  local bin="$1"
  local libpath=""
  [ -f "$bin" ] || return 0

  while IFS= read -r line; do
    libpath="$(echo "$line" | awk '{if ($2=="=>") print $3; else if ($1 ~ /^\//) print $1}' | sed 's/(.*)//' | xargs)"
    [ -z "$libpath" ] && continue
    [ -f "$libpath" ] || continue
    if echo "$libpath" | grep -qE '^/(lib|lib64|usr/lib|usr/lib64)/'; then
      continue
    fi
    LIBS_TO_COPY["$libpath"]=1
  done < <(ldd "$bin" 2>/dev/null || true)
}

add_binary_deps "${APPDIR}/usr/bin/ffmpeg_converter_gui"
for tool in ffmpeg ffprobe mkvmerge MP4Box; do
  if [ -x "${APPDIR}/usr/bin/${tool}" ]; then
    add_binary_deps "${APPDIR}/usr/bin/${tool}"
  fi
done

for lib in "${!LIBS_TO_COPY[@]}"; do
  cp -aL "$lib" "${APPDIR}/usr/lib/"
done

cat > "${APPDIR}/AppRun" << 'EOF'
#!/usr/bin/env bash
APPDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export APPDIR
export PATH="${APPDIR}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${LD_LIBRARY_PATH:-}"
if [ -x "${APPDIR}/usr/bin/ffmpeg" ]; then
  export FFMPEG="${APPDIR}/usr/bin/ffmpeg"
  export FFMPEG_BIN="${APPDIR}/usr/bin/ffmpeg"
fi
if [ -x "${APPDIR}/usr/bin/ffprobe" ]; then
  export FFPROBE="${APPDIR}/usr/bin/ffprobe"
  export FFPROBE_BIN="${APPDIR}/usr/bin/ffprobe"
fi
if [ -x "${APPDIR}/usr/bin/mkvmerge" ]; then
  export MKVMERGE="${APPDIR}/usr/bin/mkvmerge"
  export MKVMERGE_BIN="${APPDIR}/usr/bin/mkvmerge"
fi
if [ -x "${APPDIR}/usr/bin/MP4Box" ]; then
  export MP4BOX="${APPDIR}/usr/bin/MP4Box"
  export MP4BOX_BIN="${APPDIR}/usr/bin/MP4Box"
fi
if [ -f "${APPDIR}/usr/share/ffmpeg_converter/presets.json" ]; then
  export PRESETS_PATH="${APPDIR}/usr/share/ffmpeg_converter"
fi
exec "${APPDIR}/usr/bin/ffmpeg_converter_gui" "$@"
EOF
chmod +x "${APPDIR}/AppRun"

ICON_NAME="ffmpeg-converter"
ICON_SOURCE="${REPO_ROOT}/src/gui/icon.png"
if [ ! -f "${ICON_SOURCE}" ]; then
  echo "ERROR: icon.png not found at ${ICON_SOURCE}"
  exit 1
fi
cp "${ICON_SOURCE}" "${APPDIR}/usr/share/icons/hicolor/1024x1024/apps/${ICON_NAME}.png"
cp "${ICON_SOURCE}" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/${ICON_NAME}.png"
cp "${APPDIR}/usr/share/icons/hicolor/256x256/apps/${ICON_NAME}.png" "${APPDIR}/${ICON_NAME}.png"
cp "${APPDIR}/${ICON_NAME}.png" "${APPDIR}/.DirIcon"

cat > "${APPDIR}/${ICON_NAME}.desktop" << EOF
[Desktop Entry]
Type=Application
Name=FFmpeg Converter (FPC)
Comment=Convert video/audio using FFmpeg
Exec=ffmpeg_converter_gui
Icon=${ICON_NAME}
Categories=AudioVideo;Video;
Terminal=false
StartupWMClass=ffmpeg_converter_gui
EOF
cp "${APPDIR}/${ICON_NAME}.desktop" "${APPDIR}/usr/share/applications/${ICON_NAME}.desktop"

"${APPIMAGETOOL_BIN}" "${APPDIR}" "${APPIMAGE_PATH}"
echo "Created ${APPIMAGE_PATH}"
