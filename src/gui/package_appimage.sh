#!/usr/bin/env bash
#
# package_appimage.sh — Create AppImage for ffmpeg_converter_gui
#
# Usage: package_appimage.sh [build_dir]
#   build_dir — path to CMake build directory (default: ../build relative to script)
#
# Requires: appimagetool (https://github.com/AppImage/AppImageKit) in PATH
# Creates: ffmpeg_converter_gui-<arch>.AppImage in script directory
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${1:-${SCRIPT_DIR}/../build}"

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
APPIMAGE_NAME="ffmpeg_converter_gui-$(uname -m).AppImage"
APPDIR="${SCRIPT_DIR}/AppDir"

# Check GUI binary
if [ ! -x "${GUI_BIN}" ]; then
    echo "ERROR: GUI binary not found or not executable: ${GUI_BIN}"
    echo "Build the linux_gui target first: cmake --build ${ABS_BUILD_DIR} --target linux_gui"
    exit 1
fi

# Check appimagetool
if ! command -v appimagetool &>/dev/null; then
    echo "ERROR: appimagetool not found in PATH."
    echo "Install from: https://github.com/AppImage/AppImageKit"
    exit 1
fi

# Clean previous AppDir
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin"
mkdir -p "${APPDIR}/usr/lib"
mkdir -p "${APPDIR}/usr/share/applications"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"

# Copy GUI binary
echo "Copying gui binary..."
cp "${GUI_BIN}" "${APPDIR}/usr/bin/ffmpeg_converter_gui"
chmod +x "${APPDIR}/usr/bin/ffmpeg_converter_gui"

# Copy bundled tools (ffmpeg, ffprobe, mkvmerge, MP4Box) if present
echo "Copying bundled tools..."
for tool in ffmpeg ffprobe mkvmerge MP4Box; do
    if [ -x "${BIN_DIR}/${tool}" ]; then
        cp "${BIN_DIR}/${tool}" "${APPDIR}/usr/bin/"
        chmod +x "${APPDIR}/usr/bin/${tool}"
        echo "  ${tool} → bundled"
    else
        echo "  ${tool} — not found in bin/, skipping"
    fi
done

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

# Prefer bundled ffmpeg/ffprobe if they exist next to our binary
export PATH="${APPDIR}/usr/bin:${PATH}"
# Ensure bundled libs are found first
export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${LD_LIBRARY_PATH}"

# Run GUI with bundled libraries
exec "${APPDIR}/usr/bin/ffmpeg_converter_gui" "$@"
APPEOF
chmod +x "${APPDIR}/AppRun"

# Create desktop file
echo "Creating desktop entry..."
DESKTOP_FILE="${APPDIR}/ffmpeg_converter_gui.desktop"
cat > "${DESKTOP_FILE}" << DESKTOP_EOF
[Desktop Entry]
Type=Application
Name=FFmpeg Converter
Comment=Convert video/audio using FFmpeg with audio normalization
Exec=ffmpeg_converter_gui
Categories=AudioVideo;Video;
Terminal=false
StartupWMClass=ffmpeg_converter_gui
DESKTOP_EOF

# Copy icon if available
ICON_SRC="${SCRIPT_DIR}/icon.png"
if [ -f "${ICON_SRC}" ]; then
    cp "${ICON_SRC}" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/ffmpeg_converter.png"
    # Add Icon entry to desktop file
    sed -i '/^Exec=/a Icon=ffmpeg_converter' "${DESKTOP_FILE}"
    # Create symlink for icon in AppDir root (required by AppImage)
    ln -sf "usr/share/icons/hicolor/256x256/apps/ffmpeg_converter.png" "${APPDIR}/ffmpeg_converter.png"
else
    echo "No icon.png found — desktop entry will have no Icon key"
fi

# Build AppImage
echo "Building AppImage..."
cd "${SCRIPT_DIR}"
if appimagetool "${APPDIR}" "${APPIMAGE_NAME}"; then
    echo "=== Success: ${APPIMAGE_NAME} ==="
    ls -lh "${APPIMAGE_NAME}"
else
    echo "ERROR: appimagetool failed"
    exit 1
fi


