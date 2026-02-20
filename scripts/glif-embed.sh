#!/usr/bin/env bash
# glif-embed.sh — Bundle a .glif file + WASM player into a self-contained HTML file.
#
# Usage: ./scripts/glif-embed.sh input.glif [-o output.html]
#
# The output is a single HTML file with the WASM module, WASM binary, and
# .glif data all inlined as base64. No server required — just open in a browser.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
WEB_DIR="$PROJECT_DIR/web"

WASM_JS="$WEB_DIR/glif-player-wasm.js"
WASM_BIN="$WEB_DIR/glif-player-wasm.wasm"

usage() {
    echo "Usage: $0 <input.glif> [-o output.html]"
    echo ""
    echo "Bundles a .glif file into a self-contained HTML video player."
    echo "Requires: make wasm-player (produces web/glif-player-wasm.js + .wasm)"
    exit 1
}

# Parse args
INPUT=""
OUTPUT=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        -o) OUTPUT="$2"; shift 2 ;;
        -h|--help) usage ;;
        *)
            if [[ -z "$INPUT" ]]; then
                INPUT="$1"; shift
            else
                echo "Error: unexpected argument: $1" >&2; usage
            fi
            ;;
    esac
done

[[ -z "$INPUT" ]] && usage
[[ -f "$INPUT" ]] || { echo "Error: file not found: $INPUT" >&2; exit 1; }
[[ -f "$WASM_JS" ]] || { echo "Error: $WASM_JS not found. Run 'make wasm-player' first." >&2; exit 1; }
[[ -f "$WASM_BIN" ]] || { echo "Error: $WASM_BIN not found. Run 'make wasm-player' first." >&2; exit 1; }

# Default output name
if [[ -z "$OUTPUT" ]]; then
    OUTPUT="$(basename "$INPUT" .glif).html"
fi

NAME="$(basename "$INPUT")"
GLIF_SIZE=$(wc -c < "$INPUT" | tr -d ' ')
WASM_SIZE=$(wc -c < "$WASM_BIN" | tr -d ' ')

echo "Embedding: $NAME ($GLIF_SIZE bytes)"
echo "WASM binary: $WASM_SIZE bytes"
echo "Output: $OUTPUT"

# Base64 encode
echo "Encoding .glif data..."
GLIF_B64=$(base64 < "$INPUT" | tr -d '\n')
echo "Encoding WASM binary..."
WASM_B64=$(base64 < "$WASM_BIN" | tr -d '\n')
echo "Reading WASM JS loader..."
WASM_JS_CONTENT=$(cat "$WASM_JS")

# Calculate sizes
GLIF_B64_LEN=${#GLIF_B64}
WASM_B64_LEN=${#WASM_B64}

echo "Generating HTML..."

cat > "$OUTPUT" << 'EMBED_EOF'
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
EMBED_EOF

echo "<title>$(basename "$INPUT" .glif) — Glif Player</title>" >> "$OUTPUT"

cat >> "$OUTPUT" << 'EMBED_EOF'
<style>
:root {
  --bg: #0a0a0a;
  --surface: #141414;
  --border: #2a2a2a;
  --text: #e0e0e0;
  --text-dim: #666;
  --accent: #4a9eff;
  --accent-hover: #6ab4ff;
}
* { margin: 0; padding: 0; box-sizing: border-box; }
html, body { height: 100%; background: var(--bg); color: var(--text); font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', system-ui, sans-serif; overflow: hidden; }

.player-wrap {
  display: flex; flex-direction: column; height: 100vh; width: 100vw;
}

.canvas-area {
  flex: 1; display: flex; align-items: center; justify-content: center;
  position: relative; overflow: hidden; background: #000; cursor: pointer;
  min-height: 0;
}
canvas { display: block; max-width: 100%; max-height: 100%; object-fit: contain; }

.loading-overlay {
  position: absolute; inset: 0; display: flex; flex-direction: column;
  align-items: center; justify-content: center; gap: 16px;
  background: rgba(0,0,0,0.95); z-index: 10;
  transition: opacity 0.3s;
}
.loading-overlay.hidden { opacity: 0; pointer-events: none; }
.loading-overlay .spinner {
  width: 40px; height: 40px; border: 3px solid var(--border);
  border-top-color: var(--accent); border-radius: 50%;
  animation: spin 0.8s linear infinite;
}
@keyframes spin { to { transform: rotate(360deg); } }
.loading-overlay .label { font-size: 16px; color: var(--text-dim); }

.controls {
  display: flex; align-items: center; gap: 12px;
  padding: 10px 16px; background: var(--surface);
  border-top: 1px solid var(--border); flex-shrink: 0;
  user-select: none;
}

.btn {
  background: none; border: none; color: var(--text); cursor: pointer;
  padding: 6px; border-radius: 6px; display: flex; align-items: center;
  justify-content: center; transition: background 0.15s;
}
.btn:hover { background: rgba(255,255,255,0.08); }
.btn svg { width: 20px; height: 20px; fill: currentColor; }

.progress-wrap {
  flex: 1; height: 32px; display: flex; align-items: center; cursor: pointer;
  position: relative;
}
.progress-track {
  width: 100%; height: 4px; background: var(--border); border-radius: 2px;
  position: relative; transition: height 0.15s;
}
.progress-wrap:hover .progress-track { height: 6px; }
.progress-fill {
  height: 100%; background: var(--accent); border-radius: 2px;
  position: absolute; top: 0; left: 0; pointer-events: none;
}

.time { font-size: 13px; color: var(--text-dim); font-variant-numeric: tabular-nums; white-space: nowrap; min-width: 90px; }

.speed-select {
  background: var(--surface); color: var(--text-dim); border: 1px solid var(--border);
  border-radius: 4px; padding: 4px 6px; font-size: 12px; cursor: pointer;
  outline: none;
}
.speed-select:hover { border-color: #444; color: var(--text); }

.info-bar {
  display: flex; justify-content: space-between; align-items: center;
  padding: 4px 16px 6px; background: var(--surface); font-size: 11px;
  color: var(--text-dim); border-top: 1px solid var(--border); flex-shrink: 0;
}
.info-bar.hidden { display: none; }
.info-bar a { color: var(--text-dim); text-decoration: none; }
.info-bar a:hover { color: var(--accent); }
</style>
</head>
<body>
<div class="player-wrap" id="playerWrap">
  <div class="canvas-area" id="canvasArea">
    <div class="loading-overlay" id="loadingOverlay">
      <div class="spinner"></div>
      <div class="label">Loading...</div>
    </div>
    <canvas id="glif-player-canvas" width="1280" height="720"></canvas>
  </div>

  <div class="controls" id="controlBar">
    <button class="btn" id="playPause" title="Play/Pause (Space)">
      <svg id="iconPlay" viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg>
      <svg id="iconPause" viewBox="0 0 24 24" style="display:none"><path d="M6 4h4v16H6zm8 0h4v16h-4z"/></svg>
    </button>

    <span class="time" id="timeDisplay">0:00 / 0:00</span>

    <div class="progress-wrap" id="progressWrap">
      <div class="progress-track">
        <div class="progress-fill" id="progressFill" style="width:0%"></div>
      </div>
    </div>

    <select class="speed-select" id="speed" title="Playback speed">
      <option value="0.25">0.25x</option>
      <option value="0.5">0.5x</option>
      <option value="1" selected>1x</option>
      <option value="2">2x</option>
      <option value="4">4x</option>
    </select>

    <button class="btn" id="fullscreenBtn" title="Fullscreen (F)">
      <svg viewBox="0 0 24 24"><path d="M8 3H5a2 2 0 00-2 2v3m18 0V5a2 2 0 00-2-2h-3m0 18h3a2 2 0 002-2v-3M3 16v3a2 2 0 002 2h3" fill="none" stroke="currentColor" stroke-width="2"/></svg>
    </button>
  </div>

  <div class="info-bar hidden" id="infoBar">
    <span id="infoText"></span>
    <a href="https://github.com/artalis-io/glif" target="_blank">glif</a>
  </div>
</div>

<script>
// --- Inline WASM binary (base64) ---
EMBED_EOF

echo "var __WASM_B64 = \"$WASM_B64\";" >> "$OUTPUT"

cat >> "$OUTPUT" << 'EMBED_EOF'
</script>
<script>
// --- Inline Emscripten WASM JS loader ---
EMBED_EOF

cat "$WASM_JS" >> "$OUTPUT"

cat >> "$OUTPUT" << 'EMBED_EOF'
</script>
<script>
// --- Inline .glif data (base64) ---
EMBED_EOF

echo "var __GLIF_B64 = \"$GLIF_B64\";" >> "$OUTPUT"
echo "var __GLIF_NAME = \"$NAME\";" >> "$OUTPUT"

cat >> "$OUTPUT" << 'EMBED_EOF'
</script>
<script>
(async function() {
    const canvas = document.getElementById('glif-player-canvas');
    const loadingOverlay = document.getElementById('loadingOverlay');
    const playPauseBtn = document.getElementById('playPause');
    const iconPlay = document.getElementById('iconPlay');
    const iconPause = document.getElementById('iconPause');
    const timeDisplay = document.getElementById('timeDisplay');
    const progressWrap = document.getElementById('progressWrap');
    const progressFill = document.getElementById('progressFill');
    const speedSel = document.getElementById('speed');
    const fullscreenBtn = document.getElementById('fullscreenBtn');
    const playerWrap = document.getElementById('playerWrap');
    const canvasArea = document.getElementById('canvasArea');
    const infoBar = document.getElementById('infoBar');
    const infoText = document.getElementById('infoText');

    // Decode WASM binary from base64
    const wasmBytes = Uint8Array.from(atob(__WASM_B64), c => c.charCodeAt(0));
    const wasmBinary = wasmBytes.buffer;

    // Initialize WASM with inline binary
    const module = await createGlifPlayer({ wasmBinary: wasmBinary });
    const dpr = window.devicePixelRatio || 1;
    module._player_init(dpr, canvas.width, canvas.height);

    // Decode .glif data
    const glifBytes = Uint8Array.from(atob(__GLIF_B64), c => c.charCodeAt(0));
    const ptr = module._malloc(glifBytes.length);
    module.HEAPU8.set(glifBytes, ptr);
    const loadResult = module._player_load(ptr, glifBytes.length);
    module._free(ptr);

    if (loadResult !== 0) {
        loadingOverlay.querySelector('.label').textContent = 'Error: failed to parse .glif';
        loadingOverlay.querySelector('.spinner').style.display = 'none';
        return;
    }

    const frames = module._player_get_frames();
    const fps = module._player_get_fps();
    const cols = module._player_get_cols();
    const rows = module._player_get_rows();
    const cellW = module._player_get_cell_w();
    const cellH = module._player_get_cell_h();

    // Playback state
    let playing = false;
    let currentFrame = 0;
    let speed = 1.0;
    let lastTime = 0;
    let accumulator = 0;
    let rafId = null;

    function formatTime(seconds) {
        const m = Math.floor(seconds / 60);
        const s = Math.floor(seconds % 60);
        return `${m}:${s.toString().padStart(2, '0')}`;
    }

    function updateUI(frame) {
        const cur = frame / fps;
        const total = frames / fps;
        timeDisplay.textContent = `${formatTime(cur)} / ${formatTime(total)}`;
        progressFill.style.width = `${(frame / (frames - 1)) * 100}%`;
    }

    function syncPlayIcon() {
        if (playing) {
            iconPlay.style.display = 'none';
            iconPause.style.display = '';
        } else {
            iconPlay.style.display = '';
            iconPause.style.display = 'none';
        }
    }

    function sizeCanvas() {
        const aspect = cols * cellW / (rows * cellH);
        const rect = canvasArea.getBoundingClientRect();
        let w = rect.width;
        let h = rect.height;
        if (w / h > aspect) { w = h * aspect; } else { h = w / aspect; }
        const d = window.devicePixelRatio || 1;
        canvas.width = Math.round(w * d);
        canvas.height = Math.round(h * d);
        canvas.style.width = Math.round(w) + 'px';
        canvas.style.height = Math.round(h) + 'px';
        module._player_resize(canvas.width, canvas.height);
    }

    function seek(frame) {
        frame = Math.max(0, Math.min(frame, frames - 1));
        currentFrame = frame;
        module._player_decode_frame(frame);
        module._player_render();
        updateUI(frame);
    }

    function tick(now) {
        if (!playing) return;
        const dt = (now - lastTime) / 1000;
        lastTime = now;
        accumulator += dt * speed;
        const frameDur = 1 / fps;
        let advanced = false;
        while (accumulator >= frameDur) {
            accumulator -= frameDur;
            currentFrame++;
            if (currentFrame >= frames) currentFrame = 0;
            advanced = true;
        }
        if (advanced) {
            module._player_decode_frame(currentFrame);
            module._player_render();
            updateUI(currentFrame);
        }
        rafId = requestAnimationFrame(tick);
    }

    function togglePlay() {
        if (playing) {
            playing = false;
            if (rafId !== null) { cancelAnimationFrame(rafId); rafId = null; }
        } else {
            playing = true;
            lastTime = performance.now();
            accumulator = 0;
            rafId = requestAnimationFrame(tick);
        }
        syncPlayIcon();
    }

    function toggleFullscreen() {
        if (document.fullscreenElement) {
            document.exitFullscreen();
        } else {
            playerWrap.requestFullscreen().catch(function(){});
        }
    }

    // Size canvas and render first frame
    sizeCanvas();
    seek(0);

    const dur = formatTime(frames / fps);
    infoText.textContent = `${__GLIF_NAME} \u2014 ${cols}\u00d7${rows} \u00b7 ${cellW}\u00d7${cellH}px \u00b7 ${fps} fps \u00b7 ${frames} frames \u00b7 ${dur}`;
    infoBar.classList.remove('hidden');
    loadingOverlay.classList.add('hidden');

    // Auto-play
    togglePlay();

    // Event listeners
    playPauseBtn.addEventListener('click', togglePlay);
    canvasArea.addEventListener('click', function(e) {
        if (e.target === canvas || e.target === canvasArea) togglePlay();
    });

    progressWrap.addEventListener('click', function(e) {
        const rect = progressWrap.getBoundingClientRect();
        const pct = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
        seek(Math.round(pct * (frames - 1)));
    });

    speedSel.addEventListener('change', function() {
        speed = parseFloat(speedSel.value);
    });

    fullscreenBtn.addEventListener('click', toggleFullscreen);
    document.addEventListener('fullscreenchange', function() { sizeCanvas(); });

    document.addEventListener('keydown', function(e) {
        if (e.target.tagName === 'SELECT') return;
        switch (e.key) {
            case ' ':
                e.preventDefault();
                togglePlay();
                break;
            case 'f': case 'F':
                toggleFullscreen();
                break;
            case 'ArrowLeft':
                seek(Math.max(0, currentFrame - Math.round(fps * 5)));
                break;
            case 'ArrowRight':
                seek(Math.min(frames - 1, currentFrame + Math.round(fps * 5)));
                break;
            case 'ArrowUp':
                e.preventDefault();
                speed = Math.min(10, speed * 2);
                speedSel.value = speed;
                break;
            case 'ArrowDown':
                e.preventDefault();
                speed = Math.max(0.25, speed / 2);
                speedSel.value = speed;
                break;
        }
    });

    window.addEventListener('resize', function() { sizeCanvas(); });
})();
</script>
</body>
</html>
EMBED_EOF

OUTPUT_SIZE=$(wc -c < "$OUTPUT" | tr -d ' ')
echo ""
echo "Done! Self-contained HTML: $OUTPUT ($OUTPUT_SIZE bytes)"
echo "Open in any browser — no server required."
