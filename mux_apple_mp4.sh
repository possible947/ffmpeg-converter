#!/usr/bin/env bash
set -euo pipefail

# ------------------------------------------------------------
# Mux Apple-compatible MP4:
# - Video: from VIDEO input (copied as-is)
# - Audio: from AUDIO input (single source track 0:a:0 duplicated)
#   -> Track 1: AAC (q=2)
#   -> Track 2: AC3 (640k)
#
# Usage:
#   ./mux_apple_mp4.sh <video_file> <audio_file> [output_file]
#
# Example:
#   ./mux_apple_mp4.sh apple_video_only.mkv source_audio_meta.mkv output.mp4
# ------------------------------------------------------------

if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "Usage: $0 <video_file> <audio_file> [output_file]"
  exit 1
fi

VIDEO_INPUT="$1"   # файл с подготовленным apple-видео (обычно только видео дорожка)
AUDIO_INPUT="$2"   # файл с одной аудио дорожкой 0:a:0 + метаданными/главами
OUTPUT_FILE="${3:-output.mp4}"

if [[ ! -f "$VIDEO_INPUT" ]]; then
  echo "Error: video file not found: $VIDEO_INPUT"
  exit 1
fi

if [[ ! -f "$AUDIO_INPUT" ]]; then
  echo "Error: audio file not found: $AUDIO_INPUT"
  exit 1
fi

ffmpeg -y \
  -i "$AUDIO_INPUT" \
  -i "$VIDEO_INPUT" \
  -map 1:v:0 \
  -map 0:a:0 -map 0:a:0 \
  -c:v copy \
  -c:a:0 aac -q:a:0 2 \
  -c:a:1 ac3 -b:a:1 640k \
  -map_metadata 0 \
  -map_chapters 0 \
  -disposition:a:0 default -disposition:a:1 0 \
  -metadata:s:a:0 title="AAC" -metadata:s:a:0 language=rus \
  -metadata:s:a:1 title="AC3 640k" -metadata:s:a:1 language=rus \
  -movflags +faststart \
  "$OUTPUT_FILE"

echo "Done: $OUTPUT_FILE"