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

## WASM + Webcam

### C Side (compiled to WASM)

Strip out OS-dependent code — no file I/O, no OpenMP, no terminal detection. Export a clean frame-processing API:

```c
// glif_wasm.h — the WASM-exported API

typedef struct {
    void *ctx;  // Opaque handle: font DB, sampling config, precomputed masks
} GlifContext;

// One-time init — JS passes font file bytes
GlifContext *glif_init(const uint8_t *font_data, int font_len,
                             int cell_w, int cell_h,
                             float dir_crunch, float global_crunch);

// Per-frame processing
// pixels: RGB24 input (w * h * 3 bytes)
// out_chars: output character grid (rows * cols bytes)
// out_r/g/b: output color per cell (rows * cols bytes each)
// Returns: rows | (cols << 16)
int glif_process_frame(GlifContext *ctx,
                          const uint8_t *pixels, int w, int h,
                          char *out_chars,
                          uint8_t *out_r, uint8_t *out_g, uint8_t *out_b);

void glif_free(GlifContext *ctx);
```

### Build

```makefile
# Emscripten
wasm-emcc:
	emcc -O2 -Ivendor -Isrc \
	  -s EXPORTED_FUNCTIONS='["_glif_init","_glif_process_frame","_glif_free"]' \
	  -s ALLOW_MEMORY_GROWTH=1 \
	  -o glif.js \
	  src/wasm_api.c src/image.c src/sampling.c src/grid.c \
	  src/font.c src/contrast.c src/match.c
```

WASM SIMD (`wasm_simd128.h` intrinsics) replaces OpenMP SIMD for the inner loops. 128-bit SIMD is widely supported in browsers.

### JS Side

```js
const video = document.createElement('video');
const stream = await navigator.mediaDevices.getUserMedia({ video: true });
video.srcObject = stream;

const ctx = glif_init(fontBytes, fontBytes.length, 10, 20, 2.0, 2.0);

const capture = new OffscreenCanvas(640, 480);
const captureCtx = capture.getContext('2d');

function frame() {
    captureCtx.drawImage(video, 0, 0, 640, 480);
    const imageData = captureCtx.getImageData(0, 0, 640, 480);
    const rgb = rgbaToRgb(imageData.data);

    wasmMemory.set(rgb, pixelPtr);
    glif_process_frame(ctx, pixelPtr, 640, 480, charsPtr, rPtr, gPtr, bPtr);

    renderToCanvas(chars, r, g, b, cols, rows);
    requestAnimationFrame(frame);
}
```

**Rendering options (JS side):**
1. **`<pre>` with `<span>` colors** — simplest, DOM-based, works but slow at high cell counts
2. **Canvas 2D** — `fillText()` per cell with color. Fast enough for 120x40
3. **WebGL** — font atlas texture, one quad per cell, color as attribute. Fastest

### What Changes Per File

| Component | Video mode | WASM mode |
|-----------|-----------|-----------|
| image.c | Skip `stbi_load`, accept raw buffer | Same, plus accept RGBA |
| grid.c | Reuse grid allocation across frames | Same |
| sampling.c | No change | No change |
| contrast.c | No change | No change |
| match.c | No change (per-thread cache) | Single-threaded, one cache |
| output.c | Add cursor-home ANSI mode | Not used — JS renders |
| main.c | Add `--video W H` stdin loop | Replaced by `wasm_api.c` |

New file: `src/wasm_api.c` (~100 lines) wrapping the pipeline in exported functions.

---

## GPU / Shader Considerations

### Why NOT shaders for the computation pipeline

The workload is too small. A 120x40 grid is 4,800 cells — GPUs need millions of work items to amortize kernel launch overhead. At 0.69ms/frame on CPU, a GPU path would spend more time on dispatch and data transfer than actual computation.

### Where GPU DOES help: output rendering

Use a fragment shader for rendering the ASCII output — replaces the PPM renderer with real-time GPU-rendered output at arbitrary resolution:

```glsl
uniform sampler2D u_charGrid;    // 120x40, R = character index
uniform sampler2D u_colorGrid;   // 120x40, RGB = cell color
uniform sampler2D u_fontAtlas;   // glyph bitmap atlas

void main() {
    ivec2 cell = ivec2(gl_FragCoord.xy) / cellSize;
    int charIdx = int(texelFetch(u_charGrid, cell, 0).r * 255.0);
    vec3 color = texelFetch(u_colorGrid, cell, 0).rgb;

    vec2 glyphUV = getGlyphUV(charIdx, fract(gl_FragCoord.xy / cellSize));
    float alpha = texture(u_fontAtlas, glyphUV).r;

    // Color background + white glyph (default blend mode)
    gl_FragColor = vec4(color + (1.0 - color) * alpha, 1.0);
}
```

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
