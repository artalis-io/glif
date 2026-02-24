# Glif

[![CI](https://github.com/artalis-io/glif/actions/workflows/ci.yaml/badge.svg)](https://github.com/artalis-io/glif/actions/workflows/ci.yaml)

Shape-based ASCII art renderer in C. Converts images and video into ASCII art using **6D shape vectors** instead of scalar brightness, producing sharp contour-aware output with readable edges.

Supports images, live terminal video, video transcoding, webcam, and runs in the browser via WebAssembly.

![Glif example output](assets/example.png)

**[Live demo](https://glif.artalis.io)**

## How it works

Traditional ASCII art maps each cell to a character by brightness alone. Glif instead samples **6 overlapping circles** arranged in a 3x2 staggered grid within each cell, producing a 6-dimensional shape vector. Characters are matched by nearest-neighbor distance in 6D Euclidean space, so a diagonal stroke matches `/` rather than a medium-brightness character like `+`.

The pipeline:

1. **Lightness map** — Convert sRGB pixels to linear luminance via a 256-entry LUT
2. **Grid decomposition** — Divide the image into cells (default 10x20px, 2:1 aspect)
3. **Shape vectors** — Sample 6 internal circles per cell into a Vec6
4. **External sampling** — Sample 10 circles outside each cell boundary into a Vec10
5. **Directional contrast** — Diff internal vs. neighboring external samples, normalize, exponentiate
6. **Global contrast** — Normalize by max component, exponentiate
7. **Character matching** — Find nearest character Vec6 in the precomputed font database
8. **Adaptive contrast** — Per-frame percentile analysis adjusts contrast for dark/bright scenes
9. **Temporal stabilization** — EMA smoothing of normalization, shape vectors, contrast stats, and character hysteresis to reduce flicker in video

## Building

```bash
make          # build glif CLI
make wasm     # build WebAssembly (requires Emscripten)
make test     # run unit tests (185 tests)
make debug    # build with AddressSanitizer + UBSan
```

Requires a C11 compiler and OpenMP. On macOS:

```bash
brew install libomp
```

No other dependencies — `stb_image.h`, `stb_truetype.h`, Nuklear, and Clay are vendored.

## Usage

```bash
# Plain ASCII
./glif photo.png -f fonts/GeistMono-Regular.ttf

# ANSI truecolor terminal output
./glif photo.png -f fonts/GeistMono-Regular.ttf -c

# Auto-fit to terminal, colored
./glif photo.png -f fonts/GeistMono-Regular.ttf -a -c

# PPM image output (scale 4 for crisp glyphs)
./glif photo.png -f fonts/GeistMono-Regular.ttf -o output.ppm -s 4

# Dark mode PPM (black background, colored glyphs)
./glif photo.png -f fonts/GeistMono-Regular.ttf -o output.ppm --dark

# High contrast for line art
./glif diagram.png -f fonts/GeistMono-Regular.ttf -d 3.0 -g 3.0
```

A monospace TTF font is required via `-f`. The font's glyph shapes directly affect output quality — different fonts produce different results.

## Options

| Flag | Description | Default |
|------|-------------|---------|
| `-f, --font <path>` | Monospace TTF font (required) | — |
| `-w, --cell-width <px>` | Cell width in pixels | 10 |
| `-h, --cell-height <px>` | Cell height in pixels | 20 |
| `-d, --dir-crunch <f>` | Directional contrast exponent | 1.25 |
| `-g, --global-crunch <f>` | Global contrast exponent | 1.5 |
| `-a, --auto-fit` | Fit output to terminal size | off |
| `-c, --color` | ANSI truecolor terminal output | off |
| `-o, --output <file>` | Write PPM image file | — |
| `-s, --scale <n>` | PPM render scale | 4 |
| `--dark` | Black background + colored glyphs | off |
| `--video <W> <H>` | Video mode: read raw RGB24 frames from stdin | — |
| `--fps <n>` | Target framerate | 30 |
| `--pipe-ppm` | Video: write PPM frames to stdout | off |
| `--pipe-raw` | Video: write raw RGB24 to stdout | off |
| `--v4l2 <device>` | Video: write to v4l2loopback (Linux) | — |
| `--adapt-floor <0-255>` | Adaptive contrast noise floor | off |
| `--adapt-ceil <0-255>` | Adaptive contrast ceiling | 80 |
| `--output-glif <path>` | Video: write .glif binary file | — |
| `--compress` | Enable deflate compression for .glif output | off |
| `--quant <bits>` | RGB bits to keep (4–8, 8=off) | 6 with `--compress` |
| `--threshold <n>` | Temporal snap threshold (0–255) | 4 with `--compress` |
| `--audio` | Enable crushed PCM audio (requires `--output-glif`) | off |
| `--audio-pcm <path>` | Path to raw PCM file (s16le, mono, 44100 Hz) | — |
| `--audio-rate <hz>` | Output sample rate (2000–48000) | 8000 |
| `--audio-depth <bits>` | Output bit depth (4 or 8) | 8 |
| `--keep-audio <path>` | Embed original audio file (Opus/OGG/AAC) in .glif | — |

## Video in terminal

Pipe raw RGB24 frames from ffmpeg to render video as live ASCII art:

```bash
# Color ASCII video, auto-fit to terminal
ffmpeg -i video.mp4 -f rawvideo -pix_fmt rgb24 -s 640x480 - 2>/dev/null | \
  ./glif --video 640 480 -f fonts/GeistMono-Regular.ttf -c -a --fps 30

# Higher resolution input
ffmpeg -i video.mp4 -f rawvideo -pix_fmt rgb24 -s 640x480 - 2>/dev/null | \
  ./glif --video 640 480 -f fonts/GeistMono-Regular.ttf -c -a --fps 30 --dark

# Adaptive contrast (better for varying lighting)
ffmpeg -i video.mp4 -f rawvideo -pix_fmt rgb24 -s 640x480 - 2>/dev/null | \
  ./glif --video 640 480 -f fonts/GeistMono-Regular.ttf -c -a --fps 30 \
    --adapt-floor 5 --adapt-ceil 80
```

Uses diff-based rendering — only changed cells are emitted as ANSI escape codes. A byte-cost estimator dynamically chooses between diff and full redraw per frame.

## Video transcoding

Transcode any video into an ASCII art MP4 with audio using the convenience script:

```bash
# High-res (default) — 4x8 cells, scale 4, high contrast
./scripts/glif-transcode.sh movie.mp4

# Dark scenes — lower contrast, preserves shadow detail
./scripts/glif-transcode.sh movie.mp4 --dark

# Low-res retro — 120x40 grid, small file size
./scripts/glif-transcode.sh movie.mp4 --lo

# Custom output path
./scripts/glif-transcode.sh movie.mp4 -o output.mp4
```

The script probes the input for resolution and framerate, runs the full `ffmpeg -> glif -> ffmpeg` pipeline, and muxes the original audio track. ASCII art compresses well with x264 — large flat regions and repeated glyphs give favorable compression ratios.

| Preset | Cells | Scale | Contrast | Use case |
|--------|-------|-------|----------|----------|
| `--hi` (default) | 4x8 | 4 | 2.5 / 2.5 | Dense, crisp, high contrast |
| `--dark` | 4x8 | 4 | 1.3 / 1.3 | Dark scenes, night footage |
| `--lo` | 10x20 | 1 | 2.5 / 2.5 | Small file, retro look |

Or build the pipeline manually:

```bash
ffmpeg -i input.mp4 -f rawvideo -pix_fmt rgb24 -s 640x480 - 2>/dev/null | \
  ./glif --video 640 480 -f fonts/GeistPixel-Square.ttf --pipe-raw --dark -s 4 | \
  ffmpeg -f rawvideo -pix_fmt rgb24 -video_size 2560x1920 -framerate 30 -i - \
    -c:v libx264 -pix_fmt yuv420p output.mp4
```

## .glif capture and playback

Capture video as a compact `.glif` binary file, then play it back in the browser with the WASM player.

**Capture:**

```bash
# Capture video to .glif with deflate compression
ffmpeg -i video.mp4 -f rawvideo -pix_fmt rgb24 -s 640x480 - 2>/dev/null | \
  ./glif --video 640 480 -f fonts/GeistPixel-Square.ttf --output-glif output.glif --compress --fps 30

# Hi-res capture (4x8 cells, high contrast)
ffmpeg -i video.mp4 -f rawvideo -pix_fmt rgb24 -s 854x358 - 2>/dev/null | \
  ./glif --video 854 358 -f fonts/GeistPixel-Square.ttf -w 4 -h 8 -d 2.5 -g 2.5 \
    --output-glif output.glif --compress --dark --fps 30

# Capture with crushed PCM audio (8kHz 8-bit retro sound)
ffmpeg -i video.mp4 -f s16le -acodec pcm_s16le -ac 1 -ar 44100 /tmp/audio.pcm -y
ffmpeg -i video.mp4 -f rawvideo -pix_fmt rgb24 -s 854x358 - 2>/dev/null | \
  ./glif --video 854 358 -f fonts/GeistPixel-Square.ttf -w 4 -h 8 \
    --output-glif output.glif --compress --dark --fps 30 \
    --audio --audio-pcm /tmp/audio.pcm

# Extra crushed (4kHz 4-bit — maximum retro)
  ... --audio --audio-pcm /tmp/audio.pcm --audio-rate 4000 --audio-depth 4

# Original audio passthrough (Opus/AAC — full quality)
ffmpeg -i video.mp4 -vn -c:a libopus audio.opus -y
ffmpeg -i video.mp4 -f rawvideo -pix_fmt rgb24 -s 854x358 - 2>/dev/null | \
  ./glif --video 854 358 -f fonts/GeistPixel-Square.ttf -w 4 -h 8 \
    --output-glif output.glif --compress --dark --fps 30 \
    --keep-audio audio.opus
```

**Web player (`web/player.html`):**

Open `web/player.html` in a browser to play `.glif` files. Features:
- Drag-and-drop or click to open `.glif` files
- Play/pause, seek, and speed controls (0.25x–4x)
- Crushed PCM audio playback (toggle with A key or speaker button)
- Original audio passthrough when available (toggle with Q key or HQ button)
- HDR contrast enhancement toggle (shader-based tone mapping)
- Fullscreen support
- Keyboard shortcuts: Space (play/pause), F (fullscreen), H (HDR), A (audio), Q (original audio), arrow keys (seek/speed)
- Auto-loads `mk421.glif` if present in the same directory

```bash
make wasm-player                           # build WASM player
cd web && python3 -m http.server 8000      # serve locally
```

**Self-contained HTML embed:**

Bundle a `.glif` file into a single HTML file — no server, no external dependencies:

```bash
./scripts/glif-embed.sh video.glif                       # → video.html
./scripts/glif-embed.sh video.glif -o embed.html          # custom output name
./scripts/glif-embed.sh video.glif --video original.mp4   # embed with compare overlay
```

The output HTML inlines the WASM binary, JS player, and `.glif` data (base64-encoded) into a single file. Opens directly in any browser with auto-play, HDR toggle, and audio toggle (if the `.glif` contains audio). The `--video` flag embeds the original video for a side-by-side compare overlay (toggle with V key).

**Programmatic API (`web/glif-player.js`):**

```javascript
import { GlifPlayer } from './glif-player.js';

const player = new GlifPlayer();
await player.mount(document.getElementById('canvas'));

const response = await fetch('output.glif');
await player.loadFile(await response.arrayBuffer());

player.onframe = (frame) => console.log(`Frame ${frame}/${player.frames}`);
player.play();

// Controls
player.pause();
player.seek(42);
player.setSpeed(2.0);  // 0.1x to 10x
player.setHDR(0.7);    // 0.0 (off) to 1.0 (full effect)
player.destroy();
```

The compressed format uses per-frame optimal codec selection — each frame is encoded with whichever of 7 deflate-based codecs (plain, delta, filtered, palette, planar, and their delta variants) produces the smallest output. Delta variants use the previous frame as reference; scene changes naturally fall back to non-delta keyframes.

When `--compress` is enabled, two lossy preprocessing steps are applied by default to dramatically improve compression (override with `--quant 8 --threshold 0` to disable):
- **Color quantization** (`--quant 6`) — Drops 2 LSBs per RGB channel, creating more repeated values for deflate
- **Temporal thresholding** (`--threshold 4`) — Snaps cells whose RGB changed by ≤4 per channel to the previous frame, reducing noisy delta changes from ~79% to ~40% of cells

## Virtual webcam

Turn your webcam into a live ASCII art camera visible in Zoom, Google Meet, etc.

**Linux (v4l2loopback):**

```bash
# One-liner with convenience script
./scripts/glif-webcam.sh

# Options
./scripts/glif-webcam.sh --src /dev/video0 --dst /dev/video10 --scale 2 --dark
./scripts/glif-webcam.sh --light --font fonts/GeistMono-Regular.ttf
```

The script auto-loads `v4l2loopback` if needed and uses direct v4l2 output (2 processes) when available, falling back to a 3-process pipe.

**macOS / Windows:** Use `--pipe-raw` piped through ffmpeg to OBS Virtual Camera. See [docs/webcam.md](docs/webcam.md) for setup on all platforms.

## Chrome extension

Real-time ASCII art overlay for any web video. Works on YouTube, Vimeo, Twitch, and any page with `<video>` elements.

<a href="https://chromewebstore.google.com/detail/EXTENSION_ID">
  <img src="https://img.shields.io/badge/Chrome_Web_Store-Install-blue?logo=googlechrome&logoColor=white" alt="Install from Chrome Web Store" height="28">
</a>
<a href="https://github.com/artalis-io/glif/releases/latest">
  <img src="https://img.shields.io/badge/GitHub-Download_.zip-green?logo=github" alt="Download from GitHub Releases" height="28">
</a>

**Install from Chrome Web Store:** Click the badge above and press "Add to Chrome".

**Install from GitHub Release:** Download the `.zip` from [Releases](https://github.com/artalis-io/glif/releases/latest), unzip, open `chrome://extensions`, enable Developer Mode, click "Load unpacked", select the unzipped `extension/` folder.

**Build from source:**

```bash
make wasm-ext                          # build WASM for the extension
# Load extension/  as an unpacked extension in chrome://extensions
```

Features:
- **Video overlay** — ASCII art rendered via WebGL, positioned over the original video
- **Webcam interception** — Overrides `getUserMedia` so Zoom/Teams/Meet transmit ASCII art to other participants
- **SPA navigation** — Detects `pushState`/`replaceState` URL changes (YouTube, etc.) and re-attaches overlays
- **Deferred video detection** — MutationObserver catches `<video>` elements added after page load
- **Persistence** — Enabled state auto-restores on new tabs and page refreshes
- **Real-time controls** — Edge/contrast sliders and hi-res toggle update immediately

## Language bindings

### Node.js

```bash
cd bindings/node && npm install
```

```javascript
const glif = require('@artalis/glif');
const result = glif.render('photo.png', 'fonts/GeistMono-Regular.ttf');
console.log(result.toPlainText());
```

Quick start — run the bundled examples:

```bash
cd bindings/node/examples && npm install
npm start                                     # render raccoon.jpg (plain ASCII)
node render.mjs --color                       # ANSI truecolor
node render.mjs photo.png font.ttf --color    # custom image + font
node pipeline.mjs                             # step-by-step low-level API
```

### Python

```bash
make shared
pip install bindings/python
```

```python
import glif
result = glif.render('photo.png', 'fonts/GeistMono-Regular.ttf')
print(result.to_plain_text())
```

Quick start — run the bundled examples (from `bindings/python/`):

```bash
python -m examples.render ../../images/raccoon.jpg ../../fonts/GeistMono-Regular.ttf
python -m examples.render ../../images/raccoon.jpg ../../fonts/GeistMono-Regular.ttf --color
python -m examples.pipeline ../../images/raccoon.jpg ../../fonts/GeistMono-Regular.ttf
```

## Web suite

The C core compiles to WebAssembly via Emscripten. All tools run entirely client-side — no server processing.

| Tool | URL | Description |
|------|-----|-------------|
| **Demo** | [`index.html`](https://glif.artalis.io) | Drag-drop images/video/webcam, live ASCII rendering |
| **Player** | [`player.html`](https://glif.artalis.io/player.html) | Play .glif files with audio, HDR, fullscreen |
| **Transcoder** | [`transcode.html`](https://glif.artalis.io/transcode.html) | Create .glif from video in-browser |
| **Compare** | [`compare.html`](https://glif.artalis.io/compare.html) | Side-by-side original vs ASCII via ffmpeg-wasm |

```bash
make wasm && make wasm-player && make wasm-encode   # build all WASM targets
cd web && python3 -m http.server 8000                # serve locally
```

**[Try it live at glif.artalis.io](https://glif.artalis.io)**

## Architecture

```
src/
  image.c/h         sRGB-to-linear lightness via LUT
  sampling.c/h      Circle sampling geometry and precomputed offset masks
  grid.c/h          Image-to-grid decomposition, Vec6/Vec10 computation
  font.c/h          Font rasterization and character shape database
  contrast.c/h      Directional + global + adaptive contrast enhancement
  match.c/h         Nearest-neighbor character matching with LRU cache
  output.c/h        Plain, ANSI, PPM, raw pipe, .glif binary writer
  compress.c/h      Deflate-based compression codecs (7 frame types)
  blip.c/h          Crushed PCM audio encoder (downsample + quantize)
  glif.c/h          .glif binary decoder (GlifReader)
  temporal.c/h      Temporal smoothing (normalization, shape, contrast, hysteresis)
  vec6.h            Header-only 6D/10D vector math
  main.c            CLI entry point

  platform/wasm/       WASM entry points (demo, player, extension, encoder)
  platform/linux/      v4l2loopback output

extension/             Chrome extension (Manifest V3)
web/                   Web suite (demo, player, transcoder, compare)

vendor/               Vendored single-header libraries (stb, Nuklear, Clay, utest.h)
tests/                Unit tests (185 tests across 12 test files)
scripts/              Convenience scripts for transcoding, webcam, and embed
tools/                Benchmark and diagnostic tools
```

### Rendering pipeline

```
Input image/frame
       |
  sRGB -> linear luminance (LUT)
       |
  Grid decomposition (cells)
       |
  6 internal circles -> Vec6 shape vector
  10 external circles -> Vec10 contrast vector
       |
  Directional contrast (internal vs. neighbors)
  Global contrast (normalize + exponentiate)
  Adaptive contrast (per-frame percentile scaling)
       |
  [Temporal smoothing for video]
       |
  Nearest-neighbor match in 6D space -> character
       |
  Output: ASCII | ANSI | PPM | raw | .glif | WebGL
```

## Performance

Three optimizations enable real-time throughput:

1. **sRGB LUT** — 256-entry lookup table replaces per-pixel `powf()` (21x speedup)
2. **Precomputed circle masks** — Offset tables built at startup eliminate per-pixel distance tests; interior cells skip bounds checking (4.4x speedup)
3. **OpenMP + SIMD** — All pipeline stages parallelized; inner loops use SIMD reduction (2x+ on multicore)

Benchmarks on Apple M3 Pro (11 cores), `-O2`:

| Image | Resolution | Cells | Per-frame | FPS |
|-------|-----------|-------|-----------|-----|
| raccoon.jpg | 679x679 | 2,211 | 0.69 ms | 1,440 |
| wildboar.jpg | 1100x731 | 3,960 | 0.91 ms | 1,096 |

```bash
make tools/bench
./tools/bench images/raccoon.jpg -f fonts/GeistMono-Regular.ttf
```

## Testing

```bash
make test         # 185 C unit tests across 12 modules
npm run test:ext  # Chrome extension smoke tests (requires npm install)
```

C tests cover all pipeline stages: vector math, sampling, image loading, grid computation, contrast enhancement, character matching, output formats (including lossy preprocessing round-trips), temporal smoothing, deflate compression codecs, .glif round-trip encoding/decoding, and crushed PCM audio encoding/decoding. Uses [Sheredom's utest.h](https://github.com/sheredom/utest.h) framework.

Extension tests use Puppeteer to launch Chrome with the extension loaded, navigate to a test page with a synthetic video, and verify the full overlay lifecycle: injection, overlay creation, WebGL context, params/hi-res updates, disable/re-enable, SPA navigation (`pushState`), and `replaceState` no-op.

## Why C

This question comes up, so here's the reasoning.

**The codebase processes untrusted input** (images, fonts, .glif files) through vendored C libraries (stb_image, stb_truetype, miniz). The attack surface is real. We address it with defense-in-depth rather than language-level guarantees:

- `pledge()`/`unveil()` sandboxing on OpenBSD and Linux (seccomp-bpf + Landlock) — every CLI tool locks down syscalls and filesystem visibility after arg parsing, before touching input
- `-D_FORTIFY_SOURCE=2 -fstack-protector-strong` — compile-time and runtime buffer overflow detection
- AddressSanitizer + UBSan in debug builds
- All inputs bounds-checked at system boundaries (`strtol` with full validation, size overflow checks before allocation)
- LLM-assisted auditing for memory safety, use-after-free, and missing bounds checks

**Why not Rust?** Rust solves memory safety at the type system level, which is genuinely valuable at scale — large teams, deep dependency trees, long-lived codebases with contributor churn. For a small, focused project with vendored deps and one or two authors who understand every line, the tradeoffs don't pay off:

- *FFI kills the safety story.* The core pipeline calls stb_image, stb_truetype, and miniz on every frame. Each call crosses an `unsafe` FFI boundary. You get Rust's complexity without Rust's guarantees where they matter most.
- *Borrow checker fights the domain.* `GlifReader` holds a buffer and references into it (self-referential). The video pipeline reuses allocated buffers across frames and passes raw pointers between stages. These are natural in C and a restructuring project in Rust.
- *You solve Rust problems, not domain problems.* Time spent on lifetime annotations, `Arc<Mutex<>>` wrappers, orphan rule workarounds, and `Pin`/`Unpin` incantations is time not spent on sampling geometry, contrast curves, or codec strategies.
- *Cargo supply chain risk.* A typical Rust CLI pulls 200+ transitive crates from random maintainers. One compromised crate, one typosquat — you're shipping malicious code. This project vendors 3 single-header libraries you can read end to end.
- *Compile time.* Clean build: ~2 seconds. A comparable Rust project with `image`, `clap`, `rayon`: 30–90 seconds. Fast iteration matters for exploratory work.

**Why not Zig?** Zig gets a lot right — `comptime` over macros, explicit allocators, no hidden control flow, C interop for free. For a *new* project not needing Emscripten or OpenMP, it would be a strong choice. For this project, the ecosystem isn't there yet: no stb replacements, no stable 1.0, and the WASM build relies heavily on Emscripten features (`EXPORTED_FUNCTIONS`, `MODULARIZE`, `SINGLE_FILE`) that Zig's WASM target doesn't support.

**Why not C++?** No. Everything C gives you here but with a language that actively fights simplicity. The real risk isn't technical — it's cultural. C++ doesn't have a culture of restraint. Your `parse_args` is 200 lines of clear `strcmp` chains; in C++ someone would reach for a CLI parsing library or a template-based argument parser. Both worse.

**Why not Go / Java / C#?** Managed languages add runtime weight and GC pauses for problems this project doesn't have. The per-frame pipeline runs in <1ms — Go's GC alone can exceed that. No manual memory layout control, no SIMD, and no WASM story that matches the current Emscripten setup.

**Why not Python?** About 2 frames per century.

## Tools

Standalone utilities for working with `.glif` files:

| Tool | Description |
|------|-------------|
| `glif_verify` | Validate .glif file integrity, dump frame stats |
| `glif_transcode` | Re-encode .glif files with current compression |

```bash
make tools/glif_verify tools/glif_transcode
./tools/glif_verify input.glif
./tools/glif_transcode input.glif output.glif
```

## Attribution

Implements the ASCII rendering technique from [Alex Harri's](https://alexharri.com) article **["Rendering ASCII art from images"](https://alexharri.com/blog/ascii-rendering)**. The core insight — representing characters as multi-dimensional shape vectors sampled from overlapping circles — comes from that article.

### Vendored libraries

| Library | Author | License |
|---------|--------|---------|
| [stb_image](https://github.com/nothings/stb) | Sean Barrett | Public domain |
| [stb_image_write](https://github.com/nothings/stb) | Sean Barrett | Public domain |
| [stb_truetype](https://github.com/nothings/stb) | Sean Barrett | Public domain |
| [stb_rect_pack](https://github.com/nothings/stb) | Sean Barrett | Public domain |
| [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) | Micha Mettke | MIT / Public domain |
| [Clay](https://github.com/nicbarker/clay) | Nic Barker | zlib |
| [utest.h](https://github.com/sheredom/utest.h) | Sheredom | Unlicense |
| [pledge](https://github.com/jart/pledge) | Justine Tunney | ISC |

### Fonts

A monospace TTF font is required (`-f` flag). Fonts are user-provided and not included in the repository. During development, [Geist Mono](https://vercel.com/font) (SIL Open Font License) and SF Mono (Apple proprietary, not redistributable) were used for testing.

### Images

- `raccoon.jpg` and `wildboar.jpg` — Creative Commons licensed test images
- Utility images (`checkerboard.png`, `gradient.png`, `shapes.png`, `stripes.png`) — generated, no attribution needed

## License

MIT License. Copyright (c) 2026 Mark Farkas.
