#!/usr/bin/env bash
#
# package_appimage.sh — Create AppImage for ffmpeg_converter_gui
#
# Usage: package_appimage.sh [build_dir] [output_dir]
#   build_dir  — path to CMake build directory (default: ../build relative to script)
#   output_dir — directory for resulting AppImage (default: <build_dir>/bin)
#
# Requires: appimagetool (https://github.com/AppImage/AppImageKit) in PATH
# Creates: FFMpeg-Converter-<arch>.AppImage in <output_dir>
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${1:-${SCRIPT_DIR}/../build}"
OUTPUT_DIR_ARG="${2:-}"

ABS_BUILD_DIR="$(cd "${BUILD_DIR}" 2>/dev/null && pwd || true)"
if [ -z "${ABS_BUILD_DIR}" ] || [ ! -d "${ABS_BUILD_DIR}" ]; then
    echo "ERROR: Build directory not found: ${BUILD_DIR}"
    echo "Usage: $0 [build_dir]"
    exit 1
fi

echo "=== AppImage Builder ==="
echo "Build dir: ${ABS_BUILD_DIR}"
echo "Script dir: ${SCRIPT_DIR}"

BIN_DIR="${ABS_BUILD_DIR}/bin"
GUI_BIN="${BIN_DIR}/ffmpeg_converter_gui"
PROJECT_TOOLS_DIR="${SCRIPT_DIR}/../platform/linux/bin"

if [ -n "${OUTPUT_DIR_ARG}" ]; then
    OUTPUT_DIR="${OUTPUT_DIR_ARG}"
else
    OUTPUT_DIR="${BIN_DIR}"
fi

mkdir -p "${OUTPUT_DIR}"
APPIMAGE_NAME="FFMpeg-Converter-$(uname -m).AppImage"
APPIMAGE_PATH="${OUTPUT_DIR}/${APPIMAGE_NAME}"
APPDIR="${SCRIPT_DIR}/AppDir"
# Freedesktop icon/desktop basename — MUST match gtk_window_set_icon_name()
# ("ffmpeg-converter") so GNOME dock, the applications menu, and the window
# all resolve the same icon.  Display name is independent of this key.
ICON_NAME="ffmpeg-converter"
DESKTOP_BASENAME="${ICON_NAME}.desktop"
APP_DISPLAY_NAME="FFMpeg-Converter"

# Check GUI binary
if [ ! -x "${GUI_BIN}" ]; then
    echo "ERROR: GUI binary not found or not executable: ${GUI_BIN}"
    echo "Build the linux_gui target first: cmake --build ${ABS_BUILD_DIR} --target linux_gui"
    exit 1
fi

# Clean previous AppDir
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin"
mkdir -p "${APPDIR}/usr/lib"
mkdir -p "${APPDIR}/usr/share/applications"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/1024x1024/apps"

# Copy GUI binary
echo "Copying gui binary..."
cp "${GUI_BIN}" "${APPDIR}/usr/bin/ffmpeg_converter_gui"
chmod +x "${APPDIR}/usr/bin/ffmpeg_converter_gui"

# Require project-specific ffmpeg/ffprobe from src/platform/linux/bin
if [ ! -x "${PROJECT_TOOLS_DIR}/ffmpeg" ] || [ ! -x "${PROJECT_TOOLS_DIR}/ffprobe" ]; then
    echo "ERROR: Missing required project tools in ${PROJECT_TOOLS_DIR}."
    echo "Required: ffmpeg and ffprobe (project-built binaries)."
    exit 1
fi

# Check appimagetool
APPIMAGETOOL_BIN=""
if command -v appimagetool >/dev/null 2>&1; then
    APPIMAGETOOL_BIN="$(command -v appimagetool)"
elif command -v appimagetool.AppImage >/dev/null 2>&1; then
    APPIMAGETOOL_BIN="$(command -v appimagetool.AppImage)"
else
    echo "ERROR: appimagetool/appimagetool.AppImage not found in PATH."
    echo "Install from: https://github.com/AppImage/AppImageKit"
    exit 1
fi

# Copy mandatory project tools
cp "${PROJECT_TOOLS_DIR}/ffmpeg" "${APPDIR}/usr/bin/ffmpeg"
cp "${PROJECT_TOOLS_DIR}/ffprobe" "${APPDIR}/usr/bin/ffprobe"
chmod +x "${APPDIR}/usr/bin/ffmpeg" "${APPDIR}/usr/bin/ffprobe"

# Copy optional tools (mkvmerge, MP4Box)
echo "Copying bundled tools..."
for tool in mkvmerge MP4Box; do
    if [ -x "${PROJECT_TOOLS_DIR}/${tool}" ]; then
        cp "${PROJECT_TOOLS_DIR}/${tool}" "${APPDIR}/usr/bin/${tool}"
        chmod +x "${APPDIR}/usr/bin/${tool}"
        echo "  ${tool} → bundled from project"
    elif [ -x "${BIN_DIR}/${tool}" ]; then
        cp "${BIN_DIR}/${tool}" "${APPDIR}/usr/bin/${tool}"
        chmod +x "${APPDIR}/usr/bin/${tool}"
        echo "  ${tool} → bundled from build/bin"
    elif command -v "${tool}" >/dev/null 2>&1; then
        cp "$(command -v "${tool}")" "${APPDIR}/usr/bin/${tool}"
        chmod +x "${APPDIR}/usr/bin/${tool}"
        echo "  ${tool} → bundled from system PATH"
    else
        echo "  ${tool} — not found, skipping"
    fi
done

# Collect shared library dependencies
echo "Resolving shared library dependencies..."

# Copy presets.json to AppDir
if [ -f "${BIN_DIR}/presets.json" ]; then
    echo "Copying presets.json..."
    mkdir -p "${APPDIR}/usr/share/ffmpeg_converter"
    cp "${BIN_DIR}/presets.json" "${APPDIR}/usr/share/ffmpeg_converter/presets.json"
    echo "  presets.json → bundled"
elif [ -f "${SCRIPT_DIR}/../../presets.json" ]; then
    echo "Copying presets.json..."
    mkdir -p "${APPDIR}/usr/share/ffmpeg_converter"
    cp "${SCRIPT_DIR}/../../presets.json" "${APPDIR}/usr/share/ffmpeg_converter/presets.json"
    echo "  presets.json → bundled from repository root"
else
    echo "  WARNING: presets.json not found; AppImage will use built-in fallback"
fi

# Collect shared library dependencies
echo "Resolving shared library dependencies..."

# Use associative array to deduplicate
declare -A LIBS_TO_COPY

# Function to extract library paths from ldd output and filter system libs
add_binary_deps() {
    local bin="$1"
    if [ ! -f "$bin" ]; then
        return
    fi
    # ldd output: either "  lib => /path (0x...)" or "  /path"
    while IFS= read -r line; do
        # Extract the path (third column if => exists, else first if absolute)
        libpath="$(echo "$line" | awk '{if ($2=="=>") print $3; else if ($1 ~ /^\//) print $1}' | sed 's/(.*)//' | xargs)"
        # Skip if empty or not a file
        [ -z "$libpath" ] && continue
        [ ! -f "$libpath" ] && continue
        # Skip system library paths (assumed present on all target systems)
        # Exclude: /lib, /lib64, /usr/lib, /usr/lib64 (including multiarch subdirs like /lib/x86_64-linux-gnu)
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

# Copy collected libraries
echo "Copying needed shared libraries to AppDir/usr/lib..."
for lib in "${!LIBS_TO_COPY[@]}"; do
    cp -aL "$lib" "${APPDIR}/usr/lib/"
    echo "  $(basename "$lib")"
done

# Create AppRun wrapper
echo "Creating AppRun..."
cat > "${APPDIR}/AppRun" << 'APPEOF'
#!/usr/bin/env bash
# AppImage entry point — sets up bundled environment and launches GUI

# Resolve AppDir (location of this script within the mounted image)
APPDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export APPDIR

# Prefer bundled ffmpeg/ffprobe if they exist next to our binary
export PATH="${APPDIR}/usr/bin:${PATH}"
# Ensure bundled libs are found first
export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${LD_LIBRARY_PATH}"

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

# Set preset search path to bundled presets
if [ -f "${APPDIR}/usr/share/ffmpeg_converter/presets.json" ]; then
    export PRESETS_PATH="${APPDIR}/usr/share/ffmpeg_converter"
fi

# Run GUI with bundled libraries
exec "${APPDIR}/usr/bin/ffmpeg_converter_gui" "$@"
APPEOF
chmod +x "${APPDIR}/AppRun"

# Icon is required: GNOME dock, Ubuntu panel, and the applications menu
# all resolve it from the AppImage desktop file + hicolor tree.
ICON_SRC="${SCRIPT_DIR}/icon.png"
if [ ! -f "${ICON_SRC}" ]; then
    echo "ERROR: Icon not found: ${ICON_SRC}"
    echo "src/gui/icon.png is required to package the AppImage."
    exit 1
fi

echo "Installing icon (${ICON_NAME})..."
ICON_1024="${APPDIR}/usr/share/icons/hicolor/1024x1024/apps/${ICON_NAME}.png"
ICON_256="${APPDIR}/usr/share/icons/hicolor/256x256/apps/${ICON_NAME}.png"
cp "${ICON_SRC}" "${ICON_1024}"
# Scale a 256x256 copy for desktop environments that look in that size
# directory first.  Fall back to the original if ffmpeg is unavailable.
if command -v ffmpeg >/dev/null 2>&1 && \
   ffmpeg -y -hide_banner -loglevel error -i "${ICON_SRC}" \
          -vf scale=256:256 "${ICON_256}"; then
    echo "  256x256 scaled from icon.png"
else
    cp "${ICON_SRC}" "${ICON_256}"
    echo "  256x256 = copy of original (ffmpeg scale unavailable)"
fi
# AppImage spec: icon next to the .desktop in AppDir root, same basename
# as Icon=.  A real file (not a symlink) so appimagetool always embeds it.
cp "${ICON_256}" "${APPDIR}/${ICON_NAME}.png"
cp "${ICON_256}" "${APPDIR}/.DirIcon"

# Desktop file — AppDir root (appimagetool) AND usr/share/applications
# (AppImageLauncher / applications menu integration).
echo "Creating desktop entry (${APP_DISPLAY_NAME})..."
# StartupWMClass MUST equal the GtkApplication id so GNOME can match the
# running window to this desktop file and show the icon in the dock.
DESKTOP_BODY="[Desktop Entry]
Type=Application
Name=${APP_DISPLAY_NAME}
Comment=Convert video/audio using FFmpeg with audio normalization
Exec=ffmpeg_converter_gui
Icon=${ICON_NAME}
Categories=AudioVideo;Video;
Terminal=false
StartupNotify=true
StartupWMClass=io.github.possible947.ffmpeg_converter
"
printf '%s' "${DESKTOP_BODY}" > "${APPDIR}/${DESKTOP_BASENAME}"
cp "${APPDIR}/${DESKTOP_BASENAME}" \
   "${APPDIR}/usr/share/applications/${DESKTOP_BASENAME}"

# Build AppImage
echo "Building AppImage..."
cd "${SCRIPT_DIR}"
if "${APPIMAGETOOL_BIN}" "${APPDIR}" "${APPIMAGE_PATH}"; then
    echo "=== Success: ${APPIMAGE_PATH} ==="
    ls -lh "${APPIMAGE_PATH}"
else
    echo "ERROR: appimagetool failed"
    exit 1
fi

