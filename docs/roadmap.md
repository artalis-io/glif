# Glif Roadmap

## Video Mode (CLI, pure C) ✅

Implemented. Pipe raw frames from ffmpeg — keeps glif dependency-free:

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

`--pipe-ppm` writes raw PPM frames to stdout for ffmpeg to consume:

```bash
ffmpeg -i input.mp4 -f rawvideo -pix_fmt rgb24 -s 640x480 - 2>/dev/null | \
  ./glif --video 640 480 -f fonts/SFNSMono.ttf --pipe-ppm --dark -s 2 | \
  ffmpeg -f image2pipe -framerate 30 -i - -c:v libx264 -pix_fmt yuv420p output.mp4
```

### `.glif` Binary Format ✅

Compact per-frame capture for offline storage and WebGL replay. `--output-glif <path>` in video mode.

```
Header (24 bytes, little-endian):
  Offset  Size  Field
  0       4     magic:    "GLIF"
  4       1     version:  uint8    (currently 1)
  5       1     flags:    uint8    (bit 0: dark_mode)
  6       2     cols:     uint16
  8       2     rows:     uint16
  10      2     cell_w:   uint16
  12      2     cell_h:   uint16
  14      4     fps:      float32
  18      4     frames:   uint32   (written on finish)
  22      2     reserved: uint16   (zero)

Per frame (cols × rows × 4 bytes):
  [ch, r, g, b] per cell
```

At 120×40 = 4,800 cells × 4 bytes = **19.2 KB/frame**. At 30 fps: ~576 KB/s, ~34 MB/min. Gzip compresses ~40% further. Trivially streamable.

Can run alongside terminal output (`--output-glif capture.glif -c --dark`).

#### Future: delta compression

Flag byte + only changed cells could shrink this significantly for typical video content.

### Measured Performance

| Metric | Value |
|--------|-------|
| Pipeline (lightness + vectors + contrast + match) | ~1.0 ms/frame |
| Render (diff-based ANSI) | ~0.17 ms/frame |
| Terminal parsing (bottleneck) | ~35 ms/frame |
| PPM pipe render | ~18 ms/frame |

The terminal is 95% of frame time. Diff rendering minimizes bytes sent but terminal emulator parsing speed is the hard limit.

---

## WASM + Web Frontend ✅

Implemented. The C core compiles to WebAssembly via Emscripten, sharing the same source files as the native build through conditional compilation.

### Architecture

All OpenMP pragmas are guarded with `#ifdef _OPENMP` — they compile out under `emcc`. WASM SIMD 128-bit (`wasm_simd128.h`) replaces OpenMP SIMD for the grid reduction loops in `grid.c`. OS-specific code in `main.c` (`<sys/ioctl.h>`, `<unistd.h>`, `time_now()`, `nanosleep`, terminal detection) is guarded with `#ifndef __EMSCRIPTEN__`.

### Raw Buffer APIs

Both `image.c` and `font.c` expose buffer-wrapping functions for WASM use (no file I/O needed):

- `image_load_buffer(img, data, w, h, channels)` — wraps an external pixel buffer (RGB or RGBA). `Image.owns_pixels` controls whether `image_free()` calls `stbi_image_free()`.
- `char_db_create_from_memory(db, font_data, font_len, ...)` — wraps font bytes. `CharDatabase.owns_font_data` controls freeing.

### WASM UI (`src/platform/wasm/ui.c`)

Full-featured web UI built with Nuklear (widgets) + Clay (layout), rendered via WebGL.

### Build

```bash
make wasm    # produces web/glif.js + web/glif.wasm
```

Key flags: `-msimd128`, `FULL_ES2`, `MODULARIZE` (factory function `createGlifModule`), `NO_FILESYSTEM`, `--no-entry`.

### Web Frontend (`web/`)

- **`web/glif-app.js`** — JS bridge forwarding mouse/touch/keyboard events, file picking, webcam, DPR-aware canvas sizing
- **`web/index.html`** — minimal entry point loading the WASM module

```bash
# Serve locally
cd web && python3 -m http.server 8000
```

---

## Virtual Webcam

### Raw Output Modes

Two new CLI flags for lower-overhead piping:

- `--pipe-raw` — raw RGB24 frames to stdout (no PPM headers), for `ffmpeg -f rawvideo`
- `--v4l2 <device>` — write directly to a Linux v4l2loopback device (eliminates output ffmpeg)

### v4l2loopback (Linux)

```bash
sudo modprobe v4l2loopback video_nr=10 card_label="Glif ASCII Cam" exclusive_caps=1

# Direct mode (2 processes)
ffmpeg -f v4l2 -i /dev/video0 -f rawvideo -pix_fmt rgb24 -s 640x480 - 2>/dev/null | \
  ./glif --video 640 480 -f fonts/SFNSMono.ttf --v4l2 /dev/video10 --dark -s 2

# Or use the convenience script:
./scripts/glif-webcam.sh
```

See [docs/webcam.md](webcam.md) for full setup (Linux, macOS, Windows).

---

## GPU / Shader Considerations

### Why NOT shaders for the computation pipeline

The workload is too small. A 120x40 grid is 4,800 cells — GPUs need millions of work items to amortize kernel launch overhead. At 0.69ms/frame on CPU, a GPU path would spend more time on dispatch and data transfer than actual computation.

### WebGL Font-Atlas Renderer ✅

Implemented in `web/glif-webgl.js`. Replaces per-cell `fillText()` with a single GPU draw call:

1. Generate a font atlas (16x6 grid of ASCII 32–126) as a texture
2. Upload character indices and colors as small data textures per frame
3. Fragment shader samples the atlas for each pixel, compositing colored glyphs on black background

Falls back to Canvas 2D `fillText()` if WebGL is unavailable.

### Architecture

```
+----------------------------------+
|         CPU (C / WASM)           |
|                                  |
|  video frame (RGB bytes)         |
|       |                          |
|  lightness map (LUT)    0.33ms   |
|  grid vectors (masks)   0.41ms   |
|  contrast enhance       0.08ms   |
|  character match         0.05ms   |
|       |                          |
|  char grid (120x40)              |
|  color grid (120x40 RGB)         |
+--------------+-------------------+
               |
+--------------v-------------------+
|         GPU (WebGL/Metal)        |
|                                  |
|  font atlas texture              |
|  char grid -> texture upload     |  <- 4,800 bytes
|  color grid -> texture upload    |  <- 14,400 bytes
|  fullscreen quad + frag shader   |
|       |                          |
|  crispy rendered output          |
+----------------------------------+
```

CPU does the computation (small irregular workload with branching and lookup tables). GPU does the rendering (millions of pixels with a font atlas). Interface between them is ~20KB/frame.

---

## Expected Performance

| Platform | Grid size | Throughput |
|----------|-----------|-----------|
| CLI video (M3 Pro, OpenMP) | 120x40 | 1,000+ fps (bottleneck: terminal I/O) |
| WASM (Chrome, single-thread) | 120x40 | 200-400 fps |
| WASM (Chrome, single-thread) | 64x48 webcam | 500+ fps |
| WASM + WebGL rendering | 120x40 | 1,000+ fps (GPU-rendered output) |
