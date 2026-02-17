#!/bin/bash
# glif-webcam.sh — pipe webcam through glif to a virtual camera device
set -e

SRC=/dev/video0
DST=/dev/video10
W=640
H=480
FPS=30
SCALE=2
FONT=fonts/SFNSMono.ttf
DARK=--dark
GLIF=./glif

usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --src <device>     Source webcam (default: /dev/video0)"
    echo "  --dst <device>     Virtual camera device (default: /dev/video10)"
    echo "  --width <px>       Capture width (default: 640)"
    echo "  --height <px>      Capture height (default: 480)"
    echo "  --fps <n>          Framerate (default: 30)"
    echo "  --scale <n>        Render scale (default: 2)"
    echo "  --font <path>      Font TTF path (default: fonts/SFNSMono.ttf)"
    echo "  --light            Light mode (color bg + white glyphs)"
    echo "  --help             Show this message"
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --src)    SRC="$2"; shift 2 ;;
        --dst)    DST="$2"; shift 2 ;;
        --width)  W="$2"; shift 2 ;;
        --height) H="$2"; shift 2 ;;
        --fps)    FPS="$2"; shift 2 ;;
        --scale)  SCALE="$2"; shift 2 ;;
        --font)   FONT="$2"; shift 2 ;;
        --light)  DARK=""; shift ;;
        --help)   usage ;;
        *)        echo "Unknown option: $1"; usage ;;
    esac
done

if [ ! -f "$GLIF" ]; then
    echo "error: glif binary not found at $GLIF — run 'make' first"
    exit 1
fi

if [ ! -f "$FONT" ]; then
    echo "error: font not found at $FONT"
    exit 1
fi

# Load v4l2loopback if the destination device doesn't exist
if [ ! -e "$DST" ]; then
    NUM=${DST##/dev/video}
    echo "Loading v4l2loopback (video_nr=$NUM)..."
    sudo modprobe v4l2loopback video_nr="$NUM" card_label="Glif ASCII Cam" exclusive_caps=1
    sleep 0.5
fi

# Use --v4l2 if available (direct mode, 2 processes), else fall back to --pipe-raw (3 processes)
if $GLIF --help 2>&1 | grep -q -- --v4l2; then
    echo "Direct v4l2 mode: $SRC -> glif -> $DST"
    ffmpeg -f v4l2 -framerate "$FPS" -video_size "${W}x${H}" -i "$SRC" \
        -f rawvideo -pix_fmt rgb24 -s "${W}x${H}" - 2>/dev/null | \
        $GLIF --video "$W" "$H" -f "$FONT" --v4l2 "$DST" $DARK -s "$SCALE" --fps "$FPS"
else
    # Calculate output dimensions (aligned to cell boundaries)
    CELL_W=10
    CELL_H=20
    COLS=$((W / CELL_W))
    ROWS=$((H / CELL_H))
    OUT_W=$((COLS * CELL_W * SCALE))
    OUT_H=$((ROWS * CELL_H * SCALE))

    echo "Pipe mode: $SRC -> glif (${OUT_W}x${OUT_H}) -> $DST"
    ffmpeg -f v4l2 -framerate "$FPS" -video_size "${W}x${H}" -i "$SRC" \
        -f rawvideo -pix_fmt rgb24 -s "${W}x${H}" - 2>/dev/null | \
        $GLIF --video "$W" "$H" -f "$FONT" --pipe-raw $DARK -s "$SCALE" --fps "$FPS" | \
        ffmpeg -f rawvideo -pix_fmt rgb24 -video_size "${OUT_W}x${OUT_H}" \
            -framerate "$FPS" -i - -vf format=yuv420p -f v4l2 "$DST"
fi
