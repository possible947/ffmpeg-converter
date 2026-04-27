#!/usr/bin/env bash
# linux_build.sh — Build the Linux Pascal (FPC) ffmpeg-converter CLI and/or GUI.
#
# Usage:
#   ./scripts/linux_build.sh [options]
#
# Options:
#   --clean    Remove build artefacts before building
#   --cli      Build the FPC CLI (ffmpeg_converter)  [default when no target given]
#   --gui      Build the FPC GUI (ffmpeg_converter_gui, requires lazbuild)
#   --help     Show this help and exit
#
# Environment variables honoured by the underlying Makefile:
#   FPC         Override fpc binary (default: fpc)
#   LAZBUILD    Override lazbuild binary (default: lazbuild)
#   FPCFLAGS    Override FPC compiler flags (default: -Mobjfpc -Sh -O2)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
MAKE_DIR="${REPO_ROOT}/fpc/build"

DO_CLEAN=0
DO_CLI=0
DO_GUI=0
NO_TARGETS=1

show_usage() {
    echo "Usage: $0 [options]"
    echo
    echo "Options:"
    echo "  --clean    Remove build artefacts before building"
    echo "  --cli      Build the FPC CLI (ffmpeg_converter)  [default]"
    echo "  --gui      Build the FPC GUI (ffmpeg_converter_gui, requires lazbuild)"
    echo "  --help     Show this help and exit"
    echo
    echo "Examples:"
    echo "  $0                   # incremental CLI build (most common)"
    echo "  $0 --clean --cli     # clean then build CLI"
    echo "  $0 --clean --gui     # clean then build GUI"
    echo "  $0 --cli --gui       # build both CLI and GUI"
}

for arg in "$@"; do
    case "$arg" in
        --clean)  DO_CLEAN=1 ;;
        --cli)    DO_CLI=1; NO_TARGETS=0 ;;
        --gui)    DO_GUI=1; NO_TARGETS=0 ;;
        --help|-h) show_usage; exit 0 ;;
        *)
            echo "ERROR: Unknown option: $arg" >&2
            show_usage >&2
            exit 1
            ;;
    esac
done

# Default: build CLI when no target flag is given
if [ "$NO_TARGETS" -eq 1 ]; then
    DO_CLI=1
fi

cd "$MAKE_DIR"

if [ "$DO_CLEAN" -eq 1 ]; then
    echo "=== Cleaning Pascal build artefacts ==="
    make clean
fi

if [ "$DO_CLI" -eq 1 ]; then
    echo "=== Building FPC CLI ==="
    make cli
fi

if [ "$DO_GUI" -eq 1 ]; then
    echo "=== Building FPC GUI ==="
    make gui
fi

echo "=== Done ==="
