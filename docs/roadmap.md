# Glif Roadmap

## Completed

### Core Pipeline
- Shape-based ASCII rendering (6D Vec6 matching)
- Directional + global + adaptive contrast
- Temporal stabilization (4 EMA smoothers)
- OpenMP + SIMD parallelization

### Output Formats
- Plain ASCII, ANSI truecolor, PPM image, raw RGB24 pipe
- Diff-based terminal rendering (changed cells only)
- Dark mode (black background + colored glyphs)

### .glif Format
- Binary container with 7 deflate-based compression codecs
- Lossy preprocessing (color quantization + temporal thresholding)
- GlifReader with sequential fast-path and random-access seeking
- Crushed PCM audio (BLIP v2: downsample + quantize + deflate)
- Original audio passthrough (Opus/AAC in ORIG section)

### Web / WASM
- Full rendering pipeline in browser (Nuklear + Clay + WebGL)
- .glif player with audio, HDR, fullscreen, keyboard shortcuts
- Self-contained HTML embed (glif-embed.sh)
- Browser-based video transcoder (transcode.html)
- GPU-native video compare overlay (embed + standalone)
- ES module APIs: GlifPlayer, GlifEncoder

### Platform
- Chrome extension (video overlay + webcam interception)
- Virtual webcam (v4l2loopback + pipe-raw)
- Node.js and Python language bindings

### Video Pipeline
- CLI video mode (ffmpeg pipe → terminal/PPM/raw/.glif)
- Convenience scripts (transcode, webcam, embed)
- HDR shader-based contrast enhancement

---

## Planned

### Background Color Matching

Use ANSI background color (`\033[48;2;...m`) for each cell's average color while keeping the glyph as foreground. This is the single biggest visual quality upgrade available — gives both color accuracy and edge detail. Combined with shape-based glyph matching, cells get full-color backgrounds with contour-aware character overlays.

**Flag:** `--bg-color` or enabled by default in `--color` mode.

### SVG Output

Vector export for web embedding and infinite scaling. One `<text>` element per cell, monospace font specified via CSS `font-family`. Minimal SVG — no embedded fonts, just character references.

**Flag:** `-o output.svg` (detect by extension) or `--svg`.

### 256/16-Color ANSI Fallback

Support older terminals and SSH sessions where truecolor isn't available. Map RGB to nearest xterm-256 or standard 16-color ANSI palette entry. Auto-detect via `$COLORTERM` environment variable.

**Flag:** `--color=256`, `--color=16`, or auto-detected.

---

## Non-goals

- **Half-block rendering** — `▀▄█` with fg/bg colors trades away character selection entirely for color resolution. The output is no longer ASCII art in any meaningful sense, which contradicts glif's core identity of shape-based character matching.
- **SIXEL output** — Too niche; terminal support is limited.
- **WebSocket streaming** — Speculative; no concrete use case that pipe + netcat doesn't solve.

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

---

## Local Proxy Server (In Progress)

### Problem

CORS prevents fetching video directly from external URLs in the browser. The previous YouTube paste-to-capture workflow required:
1. Popup + YouTube embed
2. Manual tab selection via `getDisplayMedia()`
3. User interaction for each capture session

This never worked reliably and has been removed.

### Solution

Add a built-in HTTP proxy server that:
1. Runs alongside glif CLI (`--proxy` flag)
2. Fetches video URLs server-side (no CORS)
3. Streams frames to the web UI via MJPEG
4. Web UI sends frames to existing glif WASM pipeline

**Orthogonal**: Uses keel as a library. No changes to core pipeline (image/grid/sampling/contrast/match/blip).

### Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        glif CLI + Server                        │
│                                                                  │
│  ┌────────────────┐      ┌──────────────────────────────────┐  │
│  │  Main CLI      │      │  HTTP Server (keel-based)       │  │
│  │  (existing)   │      │  ┌────────────────────────────┐  │  │
│  │                │      │  │ /proxy/stream?url=...    │  │  │
│  │                │─────▶│  │ (fetch video, stream mjpeg)│  │  │
│  └────────────────┘      │  └────────────────────────────┘  │  │
│         │                └──────────────────────────────────┘  │
│         │                              │                         │
└─────────┼──────────────────────────────┼─────────────────────────┘
          │                              │
          │                       HTTP (no CORS)
          ▼
┌─────────────────────────────────────────────────────────────────┐
│                      glif Web UI (browser)                      │
│                                                                  │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────────┐  │
│  │ frameproxy.js│───▶│ glif WASM   │───▶│ WebGL viewport   │  │
│  │ (new)        │    │ pipeline    │    │ (existing)       │  │
│  └──────────────┘    └──────────────┘    └──────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### Files to Create/Modify

| File | Action |
|------|--------|
| `src/proxy — header |
|.h` | Create `src/proxy.c` | Create — implementation using keel |
| `Makefile` | Modify — add proxy.o, link libkeel.a |
| `src/main.c` | Modify — add `--proxy` flag + server init |
| `web/frameproxy.js` | Create — client |
| `web/glif-app.js` | Modify — integrate proxy source |
| `web/index.html` | Modify — add URL input |
| `tests/test_proxy.c` | Create — unit tests |

### API Design

```
GET /proxy/stream?url=https://youtube.com/watch?v=XXX
→ Response: multipart/x-mixed-replace (MJPEG stream)

GET /proxy/available
→ Response: {"available": true}
```

### Testing Strategy

1. **test_proxy.c** — Test proxy module in isolation:
   - URL parsing
   - Health check endpoint
   - Frame extraction

2. **frameproxy.js** — Unit test with mock server

3. **Integration** — Existing pipeline tests unchanged
