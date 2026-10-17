#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
case "${0##*/}" in
    ffmpeg-converter-cli) ENTRY=FfmpegConverter.Cli ;;
    ffmpeg-converter-gui) ENTRY=FfmpegConverter.Gui ;;
    *) echo "Unknown Converter entry point: $0" >&2; exit 1 ;;
esac
exec "$ROOT/app/$ENTRY" "$@"
