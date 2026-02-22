#!/bin/bash
# glif-transcode.sh — transcode a video into ASCII art with audio
#
# Three presets:
#   --lo    120x40 grid, scale 1 → 1200x800 output (small file, retro look)
#   --hi    4x8 cells, scale 4, high contrast → dense crisp output (default)
#   --dark  4x8 cells, scale 4, low contrast → preserves detail in dark scenes
#
# Audio from the input is copied to the output.
set -euo pipefail

GLIF=./glif
FONT=fonts/GeistPixel-Square.ttf
CELL_W=10
CELL_H=20
PRESET=hi
SCALE=
CRF=
DIR_CRUNCH=
GLOBAL_CRUNCH=
ADAPT_FLOOR=
ADAPT_CEIL=
INPUT=
OUTPUT=
GRID_W=
GRID_H=
BLIP_AUDIO=0
KEEP_AUDIO=0
AUDIO_RATE=50
GLIF_OUTPUT=

usage() {
    cat <<'EOF'
Usage: glif-transcode.sh <input.mp4> [options] [-o output.mp4]

Presets:
  --lo                  120x40 grid, scale 1 (1200x800, small file)
  --hi                  4x8 cells, scale 4, high contrast (default, dense + crisp)
  --dark                4x8 cells, scale 4, low contrast (dark scenes, military, night)

Options:
  -o, --output <path>   Output file (default: <input>_glif.mp4)
  -f, --font <path>     Font TTF (default: fonts/GeistPixel-Square.ttf)
  -s, --scale <n>       Override render scale
  --crf <n>             x264 CRF quality (default: 23 for --hi, 28 for --lo)
  --grid <cols> <rows>  Override grid dimensions
  -d, --dir-crunch <f>  Directional contrast (hi: 2.5, dark: 1.3)
  -g, --global-crunch <f> Global contrast (hi: 2.5, dark: 1.3)
  --adapt-floor <0-255> Adaptive noise floor (hi: 5, dark: 3)
  --adapt-ceil <0-255>  Adaptive ceiling (hi: 80, dark: 60)
  --no-adaptive         Disable adaptive contrast
  --help                Show this message
EOF
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --lo)     PRESET=lo; shift ;;
        --hi)     PRESET=hi; shift ;;
        --dark)   PRESET=dark; shift ;;
        -o|--output) OUTPUT="$2"; shift 2 ;;
        -f|--font)   FONT="$2"; shift 2 ;;
        -s|--scale)  SCALE="$2"; shift 2 ;;
        --crf)       CRF="$2"; shift 2 ;;
        --grid)      GRID_W="$2"; GRID_H="$3"; shift 3 ;;
        --blip)      BLIP_AUDIO=1; shift ;;
        --keep-audio) KEEP_AUDIO=1; shift ;;
        --audio-rate) AUDIO_RATE="$2"; shift 2 ;;
        --output-glif) GLIF_OUTPUT="$2"; shift 2 ;;
        -d|--dir-crunch)    DIR_CRUNCH="$2"; shift 2 ;;
        -g|--global-crunch) GLOBAL_CRUNCH="$2"; shift 2 ;;
        --adapt-floor) ADAPT_FLOOR="$2"; shift 2 ;;
        --adapt-ceil)  ADAPT_CEIL="$2"; shift 2 ;;
        --no-adaptive) ADAPT_FLOOR=""; shift ;;
        --help)   usage ;;
        -*)       echo "Unknown option: $1" >&2; usage ;;
        *)
            if [ -z "$INPUT" ]; then
                INPUT="$1"
            else
                echo "error: multiple input files" >&2; exit 1
            fi
            shift ;;
    esac
done

if [ -z "$INPUT" ]; then
    echo "error: no input file" >&2
    usage
fi

if [ ! -f "$INPUT" ]; then
    echo "error: input file not found: $INPUT" >&2
    exit 1
fi

if [ ! -f "$GLIF" ]; then
    echo "error: glif binary not found at $GLIF — run 'make' first" >&2
    exit 1
fi

if [ ! -f "$FONT" ]; then
    echo "error: font not found at $FONT" >&2
    exit 1
fi

# Default output name
if [ -z "$OUTPUT" ]; then
    BASE="${INPUT%.*}"
    OUTPUT="${BASE}_glif.mp4"
fi

# Probe input video
PROBE=$(ffprobe -v error -select_streams v:0 \
    -show_entries stream=width,height,r_frame_rate \
    -of csv=p=0 "$INPUT")
SRC_W=$(echo "$PROBE" | cut -d, -f1)
SRC_H=$(echo "$PROBE" | cut -d, -f2)
SRC_FPS_FRAC=$(echo "$PROBE" | cut -d, -f3)
# Convert fractional fps (e.g. 30000/1001) to integer
SRC_FPS=$(echo "$SRC_FPS_FRAC" | awk -F/ '{if(NF==2) printf "%.0f", $1/$2; else print $1}')

# Check if input has audio
HAS_AUDIO=$(ffprobe -v error -select_streams a:0 \
    -show_entries stream=codec_type -of csv=p=0 "$INPUT" 2>/dev/null || true)

echo "Input: ${SRC_W}x${SRC_H} @ ${SRC_FPS} fps (audio: ${HAS_AUDIO:-none})" >&2

# Apply preset defaults
if [ "$PRESET" = "lo" ]; then
    # 120x40 grid → fixed decode size
    : "${SCALE:=1}"
    : "${CRF:=28}"
    : "${DIR_CRUNCH:=2.5}"
    : "${GLOBAL_CRUNCH:=2.5}"
    : "${ADAPT_FLOOR:=5}"
    : "${ADAPT_CEIL:=80}"
    if [ -z "$GRID_W" ]; then
        GRID_W=120
        GRID_H=40
    fi
    DECODE_W=$((GRID_W * CELL_W))
    DECODE_H=$((GRID_H * CELL_H))
elif [ "$PRESET" = "dark" ]; then
    # dark: same dense grid as hi, but low contrast to preserve dark detail
    : "${SCALE:=4}"
    : "${CRF:=23}"
    : "${DIR_CRUNCH:=1.3}"
    : "${GLOBAL_CRUNCH:=1.3}"
    : "${ADAPT_FLOOR:=3}"
    : "${ADAPT_CEIL:=60}"
    if [ -z "$GRID_W" ]; then
        GRID_W=$((SRC_W / 4))
        GRID_H=$((SRC_H / 8))
    fi
    CELL_W=4
    CELL_H=8
    DECODE_W=$((GRID_W * CELL_W))
    DECODE_H=$((GRID_H * CELL_H))
else
    # hi: use small cells (4x8) for dense grid, scale 4 for crisp output
    : "${SCALE:=4}"
    : "${CRF:=23}"
    : "${DIR_CRUNCH:=2.5}"
    : "${GLOBAL_CRUNCH:=2.5}"
    : "${ADAPT_FLOOR:=5}"
    : "${ADAPT_CEIL:=80}"
    if [ -z "$GRID_W" ]; then
        GRID_W=$((SRC_W / 4))
        GRID_H=$((SRC_H / 8))
    fi
    CELL_W=4
    CELL_H=8
    DECODE_W=$((GRID_W * CELL_W))
    DECODE_H=$((GRID_H * CELL_H))
fi

OUT_W=$((GRID_W * CELL_W * SCALE))
OUT_H=$((GRID_H * CELL_H * SCALE))

echo "Grid: ${GRID_W}x${GRID_H}, scale ${SCALE}, output ${OUT_W}x${OUT_H}" >&2
echo "Encoding: CRF ${CRF}, preset medium" >&2
echo "Output: $OUTPUT" >&2

# Build the pipeline:
#   ffmpeg (decode + scale) → glif --pipe-raw → ffmpeg (encode + mux audio)
#
# We use a temporary file for the video stream, then mux audio in a final pass.
# This avoids pipe deadlocks when ffmpeg tries to read audio and write raw video
# simultaneously from the same input.

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT
TMP_VIDEO="$TMPDIR/video.mp4"

# Auto-enable blip audio when --output-glif is used and input has audio
if [ -n "$GLIF_OUTPUT" ] && [ -n "$HAS_AUDIO" ]; then
    BLIP_AUDIO=1
fi

# Extract audio as raw PCM if blip audio encoding is requested
AUDIO_FLAGS=""
if [ "$BLIP_AUDIO" -eq 1 ] && [ -n "$HAS_AUDIO" ]; then
    echo "Extracting audio as PCM..." >&2
    ffmpeg -v warning -i "$INPUT" \
        -f s16le -acodec pcm_s16le -ac 1 -ar 44100 \
        "$TMPDIR/audio.pcm"
    AUDIO_FLAGS="--audio --audio-pcm $TMPDIR/audio.pcm --audio-rate $AUDIO_RATE"
fi

# Build .glif output flags if requested
GLIF_FLAGS=""
if [ -n "$GLIF_OUTPUT" ]; then
    GLIF_FLAGS="--output-glif $GLIF_OUTPUT --compress $AUDIO_FLAGS"
fi

echo "Pass 1: rendering ASCII art..." >&2

ffmpeg -v warning -stats -i "$INPUT" \
    -f rawvideo -pix_fmt rgb24 -s "${DECODE_W}x${DECODE_H}" - 2>/dev/null | \
    $GLIF --video "$DECODE_W" "$DECODE_H" \
        -f "$FONT" -w "$CELL_W" -h "$CELL_H" --pipe-raw --dark \
        -s "$SCALE" --fps "$SRC_FPS" \
        -d "$DIR_CRUNCH" -g "$GLOBAL_CRUNCH" \
        ${ADAPT_FLOOR:+--adapt-floor "$ADAPT_FLOOR" --adapt-ceil "$ADAPT_CEIL"} \
        $GLIF_FLAGS 2>/dev/null | \
    ffmpeg -v warning -stats \
        -f rawvideo -pix_fmt rgb24 -video_size "${OUT_W}x${OUT_H}" \
        -framerate "$SRC_FPS" -i - \
        -c:v libx264 -preset medium -crf "$CRF" -pix_fmt yuv420p \
        -movflags +faststart \
        -y "$TMP_VIDEO"

if [ -n "$HAS_AUDIO" ]; then
    echo "Pass 2: muxing audio..." >&2
    ffmpeg -v warning -stats \
        -i "$TMP_VIDEO" -i "$INPUT" \
        -c:v copy -c:a aac -b:a 128k \
        -map 0:v:0 -map 1:a:0 \
        -movflags +faststart \
        -y "$OUTPUT"
else
    mv "$TMP_VIDEO" "$OUTPUT"
fi

# Report output size
OUT_SIZE=$(ls -lh "$OUTPUT" | awk '{print $5}')
IN_SIZE=$(ls -lh "$INPUT" | awk '{print $5}')
echo "Done: $OUTPUT ($OUT_SIZE, input was $IN_SIZE)" >&2
