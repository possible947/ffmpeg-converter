#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# mux_apple_m4v.sh — Apple-compatible M4V mux: video + AAC + AC3
# =============================================================================
#
# PURPOSE
# -------
# Produces a .m4v file that plays correctly in QuickTime Player, Apple TV,
# and other Apple software. The file contains:
#   - Video:  H.264 or HEVC, stream-copied from source (no re-encode)
#   - Audio1: AAC-LC stereo (VBR q=2 ≈ 128–160 kbps) — default track
#   - Audio2: AC3 5.1 640 kbps — for surround-capable Apple devices
#
# WHY MP4Box INSTEAD OF ffmpeg FOR MUXING
# ----------------------------------------
# ffmpeg muxed directly to .mp4/.m4v can produce A/V drift (audio starts
# late or runs short by 0.5–1 s) when the source has non-zero audio
# start_time (common in MKV from mkvmerge). The root causes:
#   1. ffmpeg's interleave scheduler with -max_interleave_delta mishandles
#      the PCR/PTS relationship when audio and video timebases differ.
#   2. AC3 inside MP4 requires correct dac3 box signaling that ffmpeg
#      sometimes gets wrong for Apple players.
#
# MP4Box (GPAC) imports each track from its own self-contained MP4 scratch
# file, re-builds the sample table from scratch, and writes a cleanly
# interleaved ISO Base Media file. This eliminates the drift.
#
# METHOD
# ------
# All intermediate files are placed in a mktemp directory and cleaned up
# on exit (trap).
#
# Step 1 — video scratch (.mp4, ffmpeg -c:v copy):
#   ffmpeg extracts video into a single-track MP4. Because it's -c:v copy,
#   the avcC / hvcC boxes (SPS/PPS/VPS for QuickTime) stay intact.
#   Raw elementary stream (.h264/.h265) is NOT used: MP4Box imports those
#   as "Unknown" track type which QuickTime rejects.
#
# Step 2 — AAC scratch (.m4a, ffmpeg -c:a aac -q:a 2 -f mp4):
#   VBR AAC-LC. Using -f mp4 (not -f adts) preserves correct sample timing
#   in the container; raw ADTS import via MP4Box loses ~0.7 s at the end.
#
# Step 3 — AC3 scratch (.mp4, ffmpeg -c:a ac3 -b:a 640k -f mp4):
#   Same rationale as AAC: containerised AC3 preserves timing.
#   AC3 bitrate 640 kbps is the Apple TV maximum; use lower if needed.
#
# Step 4 — MP4Box mux:
#   -brand "M4V :0" -ab mp42 -ab isom  →  correct ftyp box for Apple
#   Each -add picks only the relevant track from its scratch file (#video
#   / #audio fragment selectors). MP4Box re-interleaves at 0.5 s chunks.
#
#   What is intentionally NOT done:
#   • asemode=v1-qt:  Would replace url data references with Mac alis
#     (aliases) in the stbl, pointing to temp files that no longer exist
#     after the script exits — QuickTime then reports "file is damaged".
#   • xps_inband:     Moves SPS/PPS from avcC into the bitstream. mpv/VLC
#     handle this fine but QuickTime requires out-of-band SPS/PPS (avcC).
#   • -max_interleave_delta 0 in ffmpeg pipelines: disables proper
#     interleaving in the MP4 muxer and was the original cause of drift.
#
# Step 5 — chapters (optional):
#   If the source has chapter metadata, they are converted to MP4Box text
#   format (H:MM:SS.mmm Title) and imported with -chap.
#
# RESULT TIMING (tested on 4K H.264 25fps 4:56 PCM source)
#   video : 296.280 s   start 0.000
#   AAC   : 296.247 s   start 0.000   Δ  −33 ms
#   AC3   : 296.240 s   start 0.001   Δ  −40 ms
#
# REQUIREMENTS
#   - ffmpeg8 (or ffmpeg7/ffmpeg6/ffmpeg) + matching ffprobe
#   - MP4Box (GPAC) — https://gpac.io
#   - python3 (stdlib only, for FPS fraction math and chapter conversion)
#
# USAGE
#   ./mux_apple_m4v.sh <input_mkv> [output_file]
#
# EXAMPLE
#   ./mux_apple_m4v.sh output_encoded.mkv movie.m4v
# =============================================================================

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <input_mkv> [output_file]"
  exit 1
fi

INPUT_MKV="$1"
OUTPUT_FILE="${2:-output.m4v}"

if [[ ! -f "$INPUT_MKV" ]]; then
  echo "Error: input file not found: $INPUT_MKV"
  exit 1
fi

if ! command -v MP4Box >/dev/null 2>&1; then
  echo "Error: MP4Box not found in PATH (install GPAC)"
  exit 1
fi

# ---------------------------------------------------------------------------
# Select ffmpeg/ffprobe pair — prefer newest available version
# ---------------------------------------------------------------------------
FFMPEG_BIN=""
FFPROBE_BIN=""
for candidate in ffmpeg8 ffmpeg7 ffmpeg6 ffmpeg; do
  if command -v "$candidate" >/dev/null 2>&1; then
    probe_candidate="${candidate/ffmpeg/ffprobe}"
    if command -v "$probe_candidate" >/dev/null 2>&1; then
      FFMPEG_BIN="$candidate"
      FFPROBE_BIN="$probe_candidate"
      break
    fi
  fi
done

if [[ -z "$FFMPEG_BIN" || -z "$FFPROBE_BIN" ]]; then
  echo "Error: ffmpeg/ffprobe not found (tried ffmpeg8/7/6/ffmpeg)"
  exit 1
fi

echo "Using: $FFMPEG_BIN / $FFPROBE_BIN / MP4Box"

# ---------------------------------------------------------------------------
# Detect FPS from source (needed by MP4Box for raw video; also for logging)
# ---------------------------------------------------------------------------
FPS_RAW=$($FFPROBE_BIN -v error -select_streams v:0 \
  -show_entries stream=avg_frame_rate \
  -of default=noprint_wrappers=1:nokey=1 "$INPUT_MKV" 2>/dev/null || echo "0/0")
if [[ "$FPS_RAW" == "0/0" || -z "$FPS_RAW" ]]; then
  FPS_RAW=$($FFPROBE_BIN -v error -select_streams v:0 \
    -show_entries stream=r_frame_rate \
    -of default=noprint_wrappers=1:nokey=1 "$INPUT_MKV" 2>/dev/null || echo "25/1")
fi
FPS=$(python3 - "$FPS_RAW" << 'PYEOF'
import sys
r = sys.argv[1]
try:
    n, d = r.split('/')
    v = float(n) / float(d) if float(d) else 25.0
except Exception:
    v = 25.0
print(f"{v:.6f}")
PYEOF
)

# ---------------------------------------------------------------------------
# Scratch directory — cleaned up on exit
# ---------------------------------------------------------------------------
WORK_DIR=$(mktemp -d -t m4v_mux_XXXXXX)
trap 'rm -rf "$WORK_DIR"' EXIT

VIDEO_MP4="$WORK_DIR/video_only.mp4"
AAC_M4A="$WORK_DIR/audio_aac.m4a"
AC3_MP4="$WORK_DIR/audio_ac3.mp4"
CHAPTERS_TXT="$WORK_DIR/chapters.txt"

# ---------------------------------------------------------------------------
# Step 1: video — copy into single-track MP4 (preserves avcC/hvcC for QT)
# ---------------------------------------------------------------------------
echo "[1/5] Copying video track (fps=${FPS})..."
$FFMPEG_BIN -y -nostdin \
  -i "$INPUT_MKV" \
  -map 0:v:0 -c:v copy \
  -an -sn -dn \
  -f mp4 \
  "$VIDEO_MP4"

# ---------------------------------------------------------------------------
# Step 2: AAC-LC VBR q=2 → MP4 container
# ---------------------------------------------------------------------------
echo "[2/5] Encoding AAC (q=2)..."
$FFMPEG_BIN -y -nostdin \
  -i "$INPUT_MKV" \
  -map 0:a:0 \
  -c:a aac -profile:a aac_low -q:a 2 \
  -f mp4 \
  "$AAC_M4A"

# ---------------------------------------------------------------------------
# Step 3: AC3 640 kbps → MP4 container
# ---------------------------------------------------------------------------
echo "[3/5] Encoding AC3 (640k)..."
$FFMPEG_BIN -y -nostdin \
  -i "$INPUT_MKV" \
  -map 0:a:0 \
  -c:a ac3 -b:a 640k \
  -f mp4 \
  "$AC3_MP4"

# ---------------------------------------------------------------------------
# Step 4: MP4Box mux — proper Apple M4V ftyp + clean interleave
# ---------------------------------------------------------------------------
echo "[4/5] Muxing with MP4Box..."
MP4Box -new \
  -brand "M4V :0" -ab mp42 -ab isom \
  -add "$VIDEO_MP4#video:fps=${FPS}:name=Video" \
  -add "$AAC_M4A#audio:name=AAC:lang=rus" \
  -add "$AC3_MP4#audio:name=AC3 640k:lang=rus" \
  "$OUTPUT_FILE"

# ---------------------------------------------------------------------------
# Step 5: chapters (optional)
# ---------------------------------------------------------------------------
echo "[5/5] Importing chapters (if any)..."
$FFPROBE_BIN -v error -print_format json -show_chapters "$INPUT_MKV" \
  > "$WORK_DIR/chapters.json"

python3 - "$WORK_DIR/chapters.json" "$CHAPTERS_TXT" << 'PYEOF'
import json, sys

src, out = sys.argv[1], sys.argv[2]
try:
    with open(src, 'r', encoding='utf-8') as f:
        data = json.load(f)
except Exception:
    sys.exit(0)

chapters = data.get('chapters') or []
if not chapters:
    sys.exit(0)

def to_hmsms(seconds: float) -> str:
    if seconds < 0:
        seconds = 0.0
    h = int(seconds // 3600)
    seconds -= h * 3600
    m = int(seconds // 60)
    seconds -= m * 60
    s = int(seconds)
    ms = int(round((seconds - s) * 1000.0))
    if ms == 1000:
        s += 1; ms = 0
    return f"{h}:{m:02d}:{s:02d}.{ms:03d}"

lines = []
for idx, ch in enumerate(chapters, start=1):
    start = float(ch.get('start_time', 0.0) or 0.0)
    title = (ch.get('tags') or {}).get('title') or f"Chapter {idx}"
    lines.append(f"{to_hmsms(start)} {title}")

with open(out, 'w', encoding='utf-8') as f:
    f.write("\n".join(lines) + "\n")
PYEOF

if [[ -s "$CHAPTERS_TXT" ]]; then
  MP4Box -chap "$CHAPTERS_TXT" "$OUTPUT_FILE"
  echo "Chapters imported"
else
  echo "No chapters found in source"
fi

echo "Done: $OUTPUT_FILE"
