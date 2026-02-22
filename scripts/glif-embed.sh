#!/usr/bin/env bash
# glif-embed.sh — Bundle a .glif file + WASM player into a self-contained HTML file.
#
# Usage: ./scripts/glif-embed.sh input.glif [-o output.html]
#
# The .glif data is base64-encoded and inlined. The WASM binary and JS loader
# are also inlined. No server required — just open the HTML in a browser.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
WEB_DIR="$PROJECT_DIR/web"

WASM_JS="$WEB_DIR/glif-player-wasm.js"
WASM_BIN="$WEB_DIR/glif-player-wasm.wasm"

# Max recommended .glif size for embedding (50MB → ~67MB b64)
MAX_GLIF_SIZE=$((50 * 1048576))

usage() {
    echo "Usage: $0 <input.glif> [-o output.html] [--force]"
    echo ""
    echo "Bundles a .glif file into a self-contained HTML video player."
    echo ""
    echo "Options:"
    echo "  -o <path>   Output HTML file (default: <input>.html)"
    echo "  --force     Skip size warning for large files"
    echo ""
    echo "Requires: make wasm-player (produces web/glif-player-wasm.js + .wasm)"
    exit 1
}

# Parse args
INPUT=""
OUTPUT=""
FORCE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        -o) OUTPUT="$2"; shift 2 ;;
        --force) FORCE=1; shift ;;
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

# Size warning
if [[ $GLIF_SIZE -gt $MAX_GLIF_SIZE ]] && [[ $FORCE -eq 0 ]]; then
    GLIF_MB=$(echo "$GLIF_SIZE" | awk '{printf "%.0f", $1/1048576}')
    echo "Warning: $NAME is ${GLIF_MB}MB — the embedded HTML may be too large for browsers."
    echo "Consider using a shorter clip or lower resolution."
    echo "Use --force to proceed anyway, or Ctrl+C to cancel."
    read -r -p "Continue? [y/N] " response
    case "$response" in
        [yY]*) ;;
        *) echo "Aborted."; exit 0 ;;
    esac
fi

echo "Embedding: $NAME ($GLIF_SIZE bytes)"
echo "WASM binary: $WASM_SIZE bytes"

# Base64 encode .glif data directly (already deflate-compressed internally)
echo "Encoding .glif data..."
GLIF_B64=$(base64 < "$INPUT" | tr -d '\n')
echo "Encoding WASM binary..."
WASM_B64=$(base64 < "$WASM_BIN" | tr -d '\n')

echo "Generating HTML → $OUTPUT"

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
.btn.active { color: var(--accent); }
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
      <div class="label" id="loadingLabel">Loading...</div>
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

    <button class="btn" id="audioBtn" title="Toggle Audio (A)" style="display:none">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="11 5 6 9 2 9 2 15 6 15 11 19"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14M15.54 8.46a5 5 0 0 1 0 7.07"/></svg>
    </button>

    <button class="btn" id="hdrBtn" title="HDR Enhancement (H)">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="5"/><line x1="12" y1="1" x2="12" y2="3"/><line x1="12" y1="21" x2="12" y2="23"/><line x1="4.22" y1="4.22" x2="5.64" y2="5.64"/><line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/><line x1="1" y1="12" x2="3" y2="12"/><line x1="21" y1="12" x2="23" y2="12"/><line x1="4.22" y1="19.78" x2="5.64" y2="18.36"/><line x1="18.36" y1="5.64" x2="19.78" y2="4.22"/></svg>
    </button>

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
EMBED_EOF

# Write the .glif data inside a non-JS script tag to avoid JS parser overhead
echo '<script type="application/octet-stream" id="glifData">' >> "$OUTPUT"
echo "$GLIF_B64" >> "$OUTPUT"
echo '</script>' >> "$OUTPUT"

echo "<script>var __GLIF_NAME = \"$NAME\";</script>" >> "$OUTPUT"

cat >> "$OUTPUT" << 'EMBED_EOF'
<script>
(async function() {
    var loadingLabel = document.getElementById('loadingLabel');
    var canvas = document.getElementById('glif-player-canvas');
    var loadingOverlay = document.getElementById('loadingOverlay');
    var playPauseBtn = document.getElementById('playPause');
    var iconPlay = document.getElementById('iconPlay');
    var iconPause = document.getElementById('iconPause');
    var timeDisplay = document.getElementById('timeDisplay');
    var progressWrap = document.getElementById('progressWrap');
    var progressFill = document.getElementById('progressFill');
    var speedSel = document.getElementById('speed');
    var hdrBtn = document.getElementById('hdrBtn');
    var audioBtn = document.getElementById('audioBtn');
    var fullscreenBtn = document.getElementById('fullscreenBtn');
    var playerWrap = document.getElementById('playerWrap');
    var canvasArea = document.getElementById('canvasArea');
    var infoBar = document.getElementById('infoBar');
    var infoText = document.getElementById('infoText');

    try {
        // Decode WASM binary from base64
        loadingLabel.textContent = 'Initializing WASM...';
        var wasmBytes = Uint8Array.from(atob(__WASM_B64), function(c) { return c.charCodeAt(0); });

        // Initialize WASM with inline binary
        var module = await createGlifPlayer({ wasmBinary: wasmBytes.buffer });
        var dpr = window.devicePixelRatio || 1;
        module._player_init(dpr, canvas.width, canvas.height);

        // Decode base64 .glif data
        loadingLabel.textContent = 'Decoding video...';
        var glifB64 = document.getElementById('glifData').textContent.trim();
        var glifBytes = Uint8Array.from(atob(glifB64), function(c) { return c.charCodeAt(0); });

        // Load into WASM player
        loadingLabel.textContent = 'Loading player...';
        var ptr = module._malloc(glifBytes.length);
        module.HEAPU8.set(glifBytes, ptr);
        var loadResult = module._player_load(ptr, glifBytes.length);
        module._free(ptr);

        if (loadResult !== 0) {
            loadingLabel.textContent = 'Error: failed to parse .glif data';
            return;
        }

        var frames = module._player_get_frames();
        var fps = module._player_get_fps();
        var cols = module._player_get_cols();
        var rows = module._player_get_rows();
        var cellW = module._player_get_cell_w();
        var cellH = module._player_get_cell_h();

        // Playback state
        var playing = false;
        var currentFrame = 0;
        var speed = 1.0;
        var lastTime = 0;
        var accumulator = 0;
        var rafId = null;
        var hdrOn = true;

        function formatTime(seconds) {
            var m = Math.floor(seconds / 60);
            var s = Math.floor(seconds % 60);
            return m + ':' + String(s).padStart(2, '0');
        }

        function updateUI(frame) {
            var cur = frame / fps;
            var total = frames / fps;
            timeDisplay.textContent = formatTime(cur) + ' / ' + formatTime(total);
            progressFill.style.width = ((frame / (frames - 1)) * 100) + '%';
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
            var aspect = cols * cellW / (rows * cellH);
            var rect = canvasArea.getBoundingClientRect();
            var w = rect.width;
            var h = rect.height;
            if (w / h > aspect) { w = h * aspect; } else { h = w / aspect; }
            var d = window.devicePixelRatio || 1;
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
            if (playing) { stopAudio(); startAudio(); }
        }

        function tick(now) {
            if (!playing) return;
            var dt = (now - lastTime) / 1000;
            lastTime = now;
            accumulator += dt * speed;
            var frameDur = 1 / fps;
            var advanced = false;
            while (accumulator >= frameDur) {
                accumulator -= frameDur;
                currentFrame++;
                if (currentFrame >= frames) currentFrame = 0;
                advanced = true;
            }
            if (advanced) {
                module._player_decode_frame(currentFrame);
                module._player_render();
                playAudioForFrame(currentFrame);
                updateUI(currentFrame);
            }
            rafId = requestAnimationFrame(tick);
        }

        function togglePlay() {
            if (playing) {
                playing = false;
                if (rafId !== null) { cancelAnimationFrame(rafId); rafId = null; }
                stopAudio();
            } else {
                playing = true;
                lastTime = performance.now();
                accumulator = 0;
                rafId = requestAnimationFrame(tick);
                startAudio();
            }
            syncPlayIcon();
        }

        function toggleHDR() {
            hdrOn = !hdrOn;
            module._player_set_hdr(hdrOn ? 0.7 : 0.0);
            hdrBtn.classList.toggle('active', hdrOn);
            if (!playing) seek(currentFrame);
        }

        // Audio state (crushed PCM playback)
        var hasAudio = false;
        try { hasAudio = !!module._player_has_audio(); } catch(e) {}
        var audioCtx = null;
        var audioGainNode = null;
        var audioBuffer = null;     // decoded AudioBuffer from crushed PCM
        var audioSource = null;     // current AudioBufferSourceNode
        var audioMuted = false;

        if (hasAudio) {
            audioBtn.style.display = '';
            audioBtn.classList.add('active');
        }

        function buildAudioBuffer() {
            if (!hasAudio) return;
            try {
                audioCtx = new (window.AudioContext || window.webkitAudioContext)();
                audioGainNode = audioCtx.createGain();
                audioGainNode.gain.value = 1;
                audioGainNode.connect(audioCtx.destination);

                var pcmPtr = module._player_get_audio_pcm_ptr();
                var pcmLen = module._player_get_audio_pcm_len();
                var sampleRate = module._player_get_audio_sample_rate();
                var bitDepth = module._player_get_audio_bit_depth();

                if (!pcmPtr || pcmLen <= 0 || sampleRate <= 0) { hasAudio = false; return; }

                audioBuffer = audioCtx.createBuffer(1, pcmLen, sampleRate);
                var channel = audioBuffer.getChannelData(0);
                var maxVal = (bitDepth === 4) ? 15 : 255;
                var mid = maxVal / 2;
                for (var i = 0; i < pcmLen; i++) {
                    channel[i] = (module.HEAPU8[pcmPtr + i] - mid) / mid;
                }
            } catch(e) { hasAudio = false; }
        }

        function startAudio() {
            if (!hasAudio || audioMuted || !audioCtx || !audioBuffer) return;
            if (audioCtx.state === 'suspended') audioCtx.resume();
            stopAudio();
            var offset = currentFrame / fps;
            if (offset >= audioBuffer.duration) return;
            audioSource = audioCtx.createBufferSource();
            audioSource.buffer = audioBuffer;
            audioSource.playbackRate.value = speed;
            audioSource.loop = true;
            audioSource.connect(audioGainNode);
            audioSource.start(0, offset);
        }

        function stopAudio() {
            if (audioSource) {
                try { audioSource.stop(); } catch(e) {}
                audioSource.disconnect();
                audioSource = null;
            }
        }

        function toggleAudio() {
            audioMuted = !audioMuted;
            audioBtn.classList.toggle('active', !audioMuted);
            if (audioGainNode) {
                audioGainNode.gain.value = audioMuted ? 0 : 1;
            }
            if (!audioMuted && playing) {
                startAudio();
            } else if (audioMuted) {
                stopAudio();
            }
        }

        function playAudioForFrame(videoFrame) {
            /* no-op: audio plays continuously via AudioBufferSourceNode */
        }

        buildAudioBuffer();

        function toggleFullscreen() {
            if (document.fullscreenElement) {
                document.exitFullscreen();
            } else {
                playerWrap.requestFullscreen().catch(function(){});
            }
        }

        // Enable HDR by default
        module._player_set_hdr(0.7);
        hdrBtn.classList.add('active');

        // Size canvas and render first frame
        sizeCanvas();
        seek(0);

        var dur = formatTime(frames / fps);
        infoText.textContent = __GLIF_NAME + ' \u2014 ' + cols + '\u00d7' + rows +
            ' \u00b7 ' + cellW + '\u00d7' + cellH + 'px \u00b7 ' + fps +
            ' fps \u00b7 ' + frames + ' frames \u00b7 ' + dur;
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
            var rect = progressWrap.getBoundingClientRect();
            var pct = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
            seek(Math.round(pct * (frames - 1)));
        });

        speedSel.addEventListener('change', function() {
            speed = parseFloat(speedSel.value);
            if (audioSource) audioSource.playbackRate.value = speed;
        });

        hdrBtn.addEventListener('click', toggleHDR);
        audioBtn.addEventListener('click', toggleAudio);
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
                case 'h': case 'H':
                    toggleHDR();
                    break;
                case 'a': case 'A':
                    toggleAudio();
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
                    if (audioSource) audioSource.playbackRate.value = speed;
                    break;
                case 'ArrowDown':
                    e.preventDefault();
                    speed = Math.max(0.25, speed / 2);
                    speedSel.value = speed;
                    if (audioSource) audioSource.playbackRate.value = speed;
                    break;
            }
        });

        window.addEventListener('resize', function() { sizeCanvas(); });

    } catch (err) {
        loadingLabel.textContent = 'Error: ' + err.message;
        console.error(err);
    }
})();
</script>
</body>
</html>
EMBED_EOF

OUTPUT_SIZE=$(wc -c < "$OUTPUT" | tr -d ' ')
OUTPUT_MB=$(echo "$OUTPUT_SIZE" | awk '{printf "%.1f", $1/1048576}')
echo ""
echo "Done! Self-contained HTML: $OUTPUT (${OUTPUT_MB}MB)"
echo "Open in any browser — no server required."
