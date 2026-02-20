# Glif Roadmap

## Completed

### Video Mode (CLI) ✅

Pipe raw frames from ffmpeg — keeps glif dependency-free:

```bash
ffmpeg -i movie.mp4 -f rawvideo -pix_fmt rgb24 -s 640x480 - | \
  ./glif --video 640 480 -f fonts/SFNSMono.ttf -c --dark --fps 30
```

### Diff-Based Rendering ✅

`FrameDiff` renderer only emits ANSI escape codes for changed cells. Uses byte-cost estimation to adaptively choose between diff path and full redraw:

- **Diff cost**: `changed × cell_cost + jumps × 9 + 4`
- **Full cost**: `3 + total × cell_cost + rows × 5`

Picks whichever is smaller. First frame always renders all cells (prev buffer is zeroed). Subsequent identical frames emit zero bytes.

### Re-encoding to Video via ffmpeg ✅

`--pipe-ppm` and `--pipe-raw` write frames to stdout for ffmpeg to consume. Convenience script `scripts/glif-transcode.sh` handles the full pipeline with presets (hi/dark/lo).

### `.glif` Binary Format ✅

Compact per-frame capture for offline storage and replay. `--output-glif <path>` in video mode.

```
Header (24 bytes, little-endian):
  Offset  Size  Field
  0       4     magic:    "GLIF"
  4       1     version:  uint8    (1)
  5       1     flags:    uint8    (bit 0: dark_mode, bit 1: compressed)
  6       2     cols:     uint16
  8       2     rows:     uint16
  10      2     cell_w:   uint16
  12      2     cell_h:   uint16
  14      4     fps:      float32
  18      4     frames:   uint32   (written on finish)
  22      2     reserved: uint16   (zero)

Uncompressed: per frame = cols × rows × 4 bytes [ch, r, g, b]
Compressed:   per frame = 5-byte envelope [type(u8), size(u32 LE)] + payload
```

### WASM + Web Frontend ✅

C core compiles to WebAssembly via Emscripten. Web UI built with Nuklear (widgets) + Clay (layout), rendered via WebGL with a font-atlas shader. HiDPI/Retina support with DPR-aware canvas and font rendering.

### Temporal Stabilization ✅

Four independent EMA smoothers reduce flicker in video/webcam:
- **NormSmoother** — smoothed normalization percentiles
- **ShapeSmoother** — temporal EMA on Vec6/Vec10 shape vectors
- **ContrastSmoother** — smoothed adaptive contrast stats
- **MatchSmoother** — character hysteresis (prevents switching on marginal distance differences)

### Virtual Webcam ✅

`--v4l2` for direct Linux v4l2loopback output, `--pipe-raw` for cross-platform piping through ffmpeg to OBS. Convenience script `scripts/glif-webcam.sh`.

### Adaptive Contrast ✅

Per-frame percentile analysis with configurable noise floor and ceiling. Reduces crunch in dark scenes, increases it in bright ones.

### Chrome Extension — Video Overlay ✅

Real-time ASCII overlay on any web video via a Chrome Manifest V3 extension. The WASM pipeline (`ext.c`) is injected into the page's main world by the background service worker using `chrome.scripting.executeScript`.

- **Overlay lifecycle** — Canvas positioned over `<video>`, frame loop via `requestVideoFrameCallback`, WebGL font-atlas rendering
- **Persistence** — Enabled state saved to `chrome.storage.local`, auto-restores on new tabs via `host_permissions`
- **SPA navigation** — Monkey-patched `pushState`/`replaceState` detect URL changes, destroy and re-create overlays (ignores same-URL state updates like YouTube playback time)
- **Deferred video detection** — `MutationObserver` on `document.body` catches `<video>` elements added after page load (30s timeout)
- **Event handshake** — `glif-ready` event eliminates the race condition between WASM load and param delivery (replaces `setTimeout`)
- **Multi-instance safe** — Canvas ID set temporarily during `ext_init`, removed after WebGL context creation

### Chrome Extension — Webcam Interception ✅

Override `navigator.mediaDevices.getUserMedia` to process webcam frames through the WASM pipeline, returning an ASCII art `MediaStream` via `canvas.captureStream(30)`. Works with Zoom Web, Microsoft Teams, and Google Meet.

- **Timing** — `webcam-proxy.js` runs as a `world: "MAIN"` content script at `document_start`, installing a transparent `getUserMedia` proxy before any page JS executes. This ensures interception works even when apps cache the `getUserMedia` reference during module initialization (Google Meet, Teams).
- **Audio passthrough** — Audio tracks from the real stream are spliced into the processed stream
- **Cleanup** — `glif-webcam-stop` clears the proxy handler and stops all processing

### Extension Test Suite ✅

Puppeteer-based smoke tests (`npm run test:ext`) that load the extension in a real Chrome instance with a synthetic video (canvas `captureStream`). Verifies: script injection, overlay creation/removal, WebGL context, params, hi-res, disable/re-enable, SPA `pushState` navigation, and `replaceState` no-op.

### `.glif` Compression ✅

Deflate-based frame compression for the `.glif` binary format. Enabled with `--compress` flag (requires `--output-glif`). Sets `GLIF_FLAG_COMPRESSED` (0x02) in the header flags. Without `--compress`, frames are stored as raw `[ch, r, g, b]` cells.

**Per-frame envelope** (5 bytes): `[frame_type(u8), payload_size(u32 LE)]`

Frame types (even = keyframe, odd = delta/P-frame):
| Type | Encoding |
|------|----------|
| `0x00` | **Deflate** — Deinterleave `[ch,r,g,b]` into 4 planes, single deflate stream |
| `0x01` | **Delta+Deflate** — XOR with previous frame, then deflate |
| `0x02` | **Filtered+Deflate** — PNG-style adaptive row filters (None/Sub/Up/Avg/Paeth) per plane, then deflate |
| `0x03` | **Delta+Filtered+Deflate** — XOR with prev, then filtered deflate |
| `0x04` | **Palette+Deflate** — Build RGB palette (≤256 colors), encode as char+index pairs, deflate |
| `0x06` | **Planar+Deflate** — Deflate each of the 4 channel planes independently |
| `0x07` | **Delta+Planar+Deflate** — XOR with prev, then per-plane deflate |

The encoder tries all applicable codecs per frame and picks the smallest. Delta variants are skipped for the first frame (no previous reference). On scene changes, delta costs spike and non-delta codecs naturally win — those frames serve as keyframes. Achieves ~2x compression on typical video content.

### `.glif` Decoder (GlifReader) ✅

Memory-buffer-based decoder for the `.glif` binary format. Reads raw and compressed files without `FILE*` — WASM-compatible. Depends only on `compress.c` and vendored miniz.

- **Sequential fast-path** — When decoding frame N after frame N-1, no walk-back needed; delta frames decode directly against the previous frame buffer
- **Random access** — For arbitrary seeks, finds the nearest preceding non-delta keyframe and decodes forward through the target frame
- **Frame index** — Built on `glif_reader_open`: uncompressed uses fixed offsets, compressed walks 5-byte envelopes sequentially
- **Round-trip verified** — Unit tests write with `GlifWriter`, read with `GlifReader`, and verify byte-exact match for all frame types

### .glif Player (WASM) ✅

Web-based player for the `.glif` binary capture format. Standalone WASM module with minimal deps (no pipeline code — just `glif.c`, `compress.c`, and `player.c`).

**C side (`src/platform/wasm/player.c`):**
- `player_init` — WebGL context + font atlas setup
- `player_load` — parse `.glif` buffer via `GlifReader`, rebuild atlas with file's cell dimensions
- `player_decode_frame` / `player_render` — decode and render via shared WebGL renderer
- Header accessors: frames, fps, cols, rows, cell_w, cell_h, flags

**JS side (`web/glif-player.js`):**
- ES module `GlifPlayer` class with `mount()`, `loadFile()`, `play()`, `pause()`, `seek()`, `setSpeed()`, `destroy()`
- `requestAnimationFrame`-based playback with accumulator-driven frame stepping
- Configurable speed (0.1x–10x), looping, and callbacks (`onload`, `onframe`, `onend`)

**Build:** `make wasm-player` produces `web/glif-player-wasm.js` + `.wasm`.

### .glif Player UI ✅

Full-featured web player (`web/player.html`) for `.glif` files with a dark modern design:

- **File loading** — Drag-and-drop or click to open `.glif` files via file picker
- **Playback controls** — Play/pause, click-to-seek progress bar, speed selector (0.25x–4x)
- **Fullscreen** — Fullscreen API with responsive canvas sizing
- **HDR enhancement** — Toggle shader-based tone mapping (H key or sun icon button)
- **Keyboard shortcuts** — Space (play/pause), F (fullscreen), H (HDR), left/right arrows (seek ±5s), up/down arrows (speed)
- **Auto-load** — Fetches `mk421.glif` if present, or accepts `window.__GLIF_EMBED_DATA` for embed mode
- **Info bar** — File metadata: grid size, cell size, fps, frame count, duration
- **DPR-aware** — Proper HiDPI/Retina canvas sizing using `devicePixelRatio`

### Self-Contained HTML Embed ✅

Bundle a `.glif` file + WASM player into a single self-contained HTML file that opens directly in any browser — no server, no external dependencies.

```bash
./scripts/glif-embed.sh video.glif                # → video.html
./scripts/glif-embed.sh video.glif -o embed.html   # custom output name
```

**How it works:**
- Base64-encodes the `.wasm` binary and `.glif` data inline
- Inlines the Emscripten JS loader and player logic
- Passes `wasmBinary` to the Emscripten factory to avoid external fetches
- Auto-plays on load with full player controls (play/pause, seek, speed, fullscreen)
- Same dark modern UI as `web/player.html` including HDR toggle

### Shared WebGL Font-Atlas Renderer ✅

Extracted the duplicated WebGL shader code from `ext.c` and `ui.c` into a shared header-only module (`src/platform/wasm/vp_render.h`). All functions `static inline` since each WASM target compiles separately.

- `VpRenderState` struct holds all GL state (program, textures, quad buffer, atlas dimensions)
- `vp_state_init` — create shader program + quad + data textures
- `vp_build_font_atlas` — rasterize 95-char atlas (16x6 grid) from TTF font data
- `vp_render_raw` — upload char/color buffers and draw (used by player)
- `vp_upload` / `vp_draw` — split upload/draw for callers needing custom viewport/scissor (used by ui.c with Clay bounds)

### HDR Contrast Enhancement ✅

Shader-based tone mapping and contrast enhancement for all WebGL playback. Controlled by a single `u_hdrIntensity` uniform (0.0 = bypass, 1.0 = full effect). Works on every existing `.glif` file — no format or encoder changes needed.

**Processing chain (fragment shader):**
1. **Gamma lift** — `pow(color, vec3(gamma))` brightens midtones without clipping highlights. Gamma interpolated from 1.0 (off) to 0.8 (full intensity).
2. **S-curve contrast** — `smoothstep` sigmoid that darkens shadows and brightens highlights, blended at 50% intensity.
3. **Saturation boost** — Luminance-based desaturation mix at 1.0 + 0.3×intensity, making ASCII art colors pop against the black background.

**API:**
- C: `player_set_hdr(float)` / `ext_set_hdr(float)` — exported WASM functions
- JS: `GlifPlayer.setHDR(intensity)` — clamps 0–1, applies immediately
- UI: Sun icon toggle button in player and embed control bars, `H` keyboard shortcut (toggles between 0.0 and 0.7)

---

## Planned

### ~~Half-Block Rendering~~ — Won't implement

Glif's core identity is **shape-based character matching** — representing images through actual ASCII glyphs selected by 6D shape vectors. Half-block rendering (`▀▄█` with fg/bg colors) trades away character selection entirely for color resolution, producing output that is no longer ASCII art in any meaningful sense. This fundamentally contradicts the project's philosophy. If you want pixel-accurate color blocks, use an image viewer.

### SVG Output

Vector export for web embedding and infinite scaling.

**Approach:**
- One `<text>` or `<rect>`+`<text>` element per cell
- Monospace font specified via CSS `font-family`
- Dark mode: colored text on black rect; light mode: white text on colored rect
- Minimal SVG — no embedded fonts, just character references

**Flag:** `-o output.svg` (detect by extension) or `--svg`.

### Web UI Export

Download button in the web UI to save the current render as PNG or SVG.

**Approach:**
- PNG: read WebGL framebuffer pixels via `gl.readPixels()`, encode with canvas `toBlob()`
- SVG: generate from the grid data (same as CLI SVG output)
- Trigger browser download via `URL.createObjectURL()`

### 256-Color and 16-Color ANSI Fallback

Support older terminals and SSH sessions where truecolor isn't available.

**Approach:**
- `--color=256`: map RGB to nearest xterm-256 palette entry
- `--color=16`: map to standard 16 ANSI colors
- Auto-detect via `$COLORTERM` environment variable (truecolor if `truecolor` or `24bit`, else check `$TERM` for 256-color support)

### Background Color Matching

Use ANSI background color (`\033[48;2;...m`) for each cell's average color while keeping the glyph as foreground.

**Approach:**
- Compute average color for the cell region
- Set background to average color, foreground to contrasting color (white or black based on luminance)
- Combined with shape-based glyph matching, this gives both color accuracy and edge detail

**Flag:** `--bg-color` or enabled by default in `--color` mode.

### Font Comparison View

Side-by-side rendering with different fonts in the web UI.

**Approach:**
- Split viewport into 2-4 panels, each with its own `CharDatabase`
- Drag-and-drop font files onto individual panels
- Shared image/video source, independent font rendering
- Useful for choosing the best font for a given use case

### Config Presets

Save and load named parameter presets.

**Approach:**
- Simple key=value text files in `~/.config/glif/` or project-local `.glifrc`
- `--preset <name>` loads a preset; `--save-preset <name>` saves current flags
- Ship default presets: `hires`, `retro`, `dark`, `webcam`

### Streaming WebSocket Mode

Pipe terminal-rendered ASCII over a WebSocket for remote viewing.

**Approach:**
- `--ws <port>` starts a WebSocket server
- Streams ANSI escape sequences or structured JSON grid data
- Browser client connects and renders in a `<pre>` or canvas
- Useful for remote monitoring, sharing renders without screen sharing

### SIXEL Output

For terminals that support SIXEL graphics (kitty, WezTerm, mlterm), render pixel-perfect glyph output inline.

**Approach:**
- Detect SIXEL support via `$TERM` or device attributes query
- Render the same PPM pixel buffer used by `--pipe-ppm`
- Encode as SIXEL escape sequence and write to stdout
- Gives crisp rendered output without leaving the terminal

**Flag:** `--sixel` or auto-detected.

---

## Design Notes

### GPU / Shader Considerations

The computation pipeline workload is too small for GPU acceleration. A 120x40 grid is 4,800 cells — GPUs need millions of work items to amortize kernel launch overhead. At 0.69ms/frame on CPU, a GPU path would spend more time on dispatch and data transfer than actual computation.

The GPU is used only for rendering (WebGL font-atlas shader), where it handles millions of output pixels efficiently.

### Performance Characteristics

| Platform | Grid size | Throughput |
|----------|-----------|-----------|
| CLI video (M3 Pro, OpenMP) | 120x40 | 1,000+ fps (bottleneck: terminal I/O) |
| WASM (Chrome, single-thread) | 120x40 | 200-400 fps |
| WASM + WebGL rendering | 120x40 | 1,000+ fps (GPU-rendered output) |

| Metric | Value |
|--------|-------|
| Pipeline (lightness + vectors + contrast + match) | ~1.0 ms/frame |
| Render (diff-based ANSI) | ~0.17 ms/frame |
| Terminal parsing (bottleneck) | ~35 ms/frame |
| PPM pipe render | ~18 ms/frame |
