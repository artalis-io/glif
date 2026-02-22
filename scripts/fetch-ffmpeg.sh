#!/usr/bin/env bash
# fetch-ffmpeg.sh — Download ffmpeg-wasm to web/vendor/ffmpeg/ for offline use.
#
# Uses @ffmpeg/ffmpeg v0.12.x (latest stable ES module + WASM core).
# Files are .gitignore'd; run this once after cloning.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
VENDOR_DIR="$PROJECT_DIR/web/vendor/ffmpeg"

FFMPEG_VERSION="0.12.10"
CORE_VERSION="0.12.6"

UNPKG="https://unpkg.com"

mkdir -p "$VENDOR_DIR"

echo "Fetching @ffmpeg/ffmpeg@${FFMPEG_VERSION}..."
curl -sL "$UNPKG/@ffmpeg/ffmpeg@${FFMPEG_VERSION}/dist/esm/index.js" \
  -o "$VENDOR_DIR/ffmpeg.js"

echo "Fetching @ffmpeg/core@${CORE_VERSION}..."
curl -sL "$UNPKG/@ffmpeg/core@${CORE_VERSION}/dist/esm/ffmpeg-core.js" \
  -o "$VENDOR_DIR/ffmpeg-core.js"
curl -sL "$UNPKG/@ffmpeg/core@${CORE_VERSION}/dist/esm/ffmpeg-core.wasm" \
  -o "$VENDOR_DIR/ffmpeg-core.wasm"

echo ""
echo "Done! Files in $VENDOR_DIR:"
ls -lh "$VENDOR_DIR"
echo ""
echo "These files are .gitignore'd. Re-run this script after a fresh clone."
