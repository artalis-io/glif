# Glif

Shape-based ASCII art renderer in C. Implements the rendering technique from [Alex Harri's article](https://alexharri.com/blog/ascii-rendering) — treating ASCII characters as **6D shape vectors** sampled from circles rather than single brightness values.

This produces dramatically sharper ASCII art with readable contours compared to traditional brightness-only mapping.

## How it works

Each cell in the output grid is matched to the ASCII character whose **shape** most closely resembles the image content in that region. Shape is captured by sampling 6 overlapping circles arranged in a 3×2 staggered grid within each cell. Each circle's average luminance becomes one component of a 6D vector.

Directional contrast enhancement (using 10 additional circles outside the cell boundary) and global contrast enhancement sharpen edges before matching.

Character shapes are precomputed from a monospace font using the same sampling circles, then matched via nearest-neighbor in 6D Euclidean space.

## Building

```bash
make
```

Requires a C11 compiler (gcc, clang) and OpenMP for parallel acceleration. On macOS:

```bash
brew install libomp
```

No other external dependencies — `stb_image.h` and `stb_truetype.h` are vendored.

A debug build with AddressSanitizer/UBSan is available via `make debug`.

## Usage

```bash
# Plain ASCII output
./glif photo.png -f fonts/MyMono.ttf

# ANSI truecolor output
./glif photo.png -f fonts/MyMono.ttf -c

# PPM image output
./glif photo.png -f fonts/MyMono.ttf -o output.ppm

# Auto-fit to terminal size
./glif photo.png -f fonts/MyMono.ttf -a

# Custom cell size and contrast
./glif photo.png -f fonts/MyMono.ttf -w 8 -h 16 -d 2.0 -g 2.0
```

You need to provide a monospace TTF font via `-f`. Any monospace font works — the font's glyph shapes directly affect output quality.

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
| `-s, --scale <n>` | PPM render scale for sharp text | 4 |
| `--dark` | PPM: black background + colored glyphs | off |
| `--video <W> <H>` | Video mode: read raw RGB24 frames from stdin | — |
| `--fps <n>` | Frame rate for video playback | 30 |
| `--pipe-ppm` | Video: write PPM frames to stdout (for ffmpeg) | off |
| `--pipe-raw` | Video: write raw RGB24 to stdout (no headers) | off |
| `--v4l2 <device>` | Video: write to v4l2loopback device (Linux only) | — |
| `--adapt-floor <0-255>` | Adaptive contrast noise floor (enables adaptive) | off |
| `--adapt-ceil <0-255>` | Adaptive contrast ceiling | 80 |
| `--output-glif <path>` | Video: write .glif binary capture file | — |

## Video Mode

Pipe raw RGB24 frames from ffmpeg to render video as live ASCII art:

```bash
# Color ASCII video, auto-fit to terminal
ffmpeg -i video.mp4 -f rawvideo -pix_fmt rgb24 -s 320x240 - 2>/dev/null | \
  ./glif --video 320 240 -f fonts/SFNSMono.ttf -c -a --fps 30

# Higher resolution input for more detail
ffmpeg -i video.mp4 -f rawvideo -pix_fmt rgb24 -s 640x480 - 2>/dev/null | \
  ./glif --video 640 480 -f fonts/SFNSMono.ttf -c -a --fps 30

# Plain ASCII (no color)
ffmpeg -i video.mp4 -f rawvideo -pix_fmt rgb24 -s 320x240 - 2>/dev/null | \
  ./glif --video 320 240 -f fonts/SFNSMono.ttf -a --fps 24
```

The `--video W H` flag tells glif to read raw RGB24 frames of size W×H from stdin. Combine with `-a` to auto-fit the grid to your terminal. Frame pacing ensures smooth playback at the specified `--fps`. On exit, glif reports total frames played and average FPS.

## Video Transcoding

Transcode any video into an ASCII art MP4 with audio using the convenience script:

```bash
# High-res (default) — matches input resolution, scale 4, crisp text
./scripts/glif-transcode.sh movie.mp4

# Dark scenes — military, night footage, preserves detail in shadows
./scripts/glif-transcode.sh movie.mp4 --dark

# Low-res retro — 120x40 grid, small file size
./scripts/glif-transcode.sh movie.mp4 --lo

# Custom settings
./scripts/glif-transcode.sh movie.mp4 --grid 80 30 -s 3 --crf 20 -o output.mp4
```

The script handles the full `ffmpeg → glif → ffmpeg` pipeline automatically: it probes the input for resolution/fps, renders through glif with `--pipe-raw`, encodes with x264, and muxes the original audio track. ASCII art compresses exceptionally well — large flat black regions and repeated glyph patterns give x264 very favorable compression ratios.

| Preset | Cells | Grid density | Scale | Contrast | Adaptive | Use case |
|--------|-------|-------------|-------|----------|----------|----------|
| `--hi` (default) | 4×8 | ~160×60 for 640p | 4 | 2.5 / 2.5 | floor 5, ceil 80 | Dense, crisp, high contrast |
| `--dark` | 4×8 | ~160×60 for 640p | 4 | 1.3 / 1.3 | floor 3, ceil 60 | Dark scenes, military, night |
| `--lo` | 10×20 | 120×40 | 1 | 2.5 / 2.5 | floor 5, ceil 80 | Small file, retro |

Or build the pipeline manually with `--pipe-raw` / `--pipe-ppm`:

```bash
ffmpeg -i input.mp4 -f rawvideo -pix_fmt rgb24 -s 640x480 - 2>/dev/null | \
  ./glif --video 640 480 -f fonts/SFNSMono.ttf --pipe-raw --dark -s 2 | \
  ffmpeg -f rawvideo -pix_fmt rgb24 -video_size 1280x960 -framerate 30 -i - \
    -c:v libx264 -pix_fmt yuv420p output.mp4
```

## Virtual Webcam

Turn your webcam into a live ASCII art camera visible in Zoom, Google Meet, etc.

**Linux (v4l2loopback):**

```bash
# One-liner with convenience script
./scripts/glif-webcam.sh

# Or manually — direct v4l2 mode (2 processes, lowest latency)
sudo modprobe v4l2loopback video_nr=10 card_label="Glif ASCII Cam" exclusive_caps=1
ffmpeg -f v4l2 -i /dev/video0 -f rawvideo -pix_fmt rgb24 -s 640x480 - 2>/dev/null | \
  ./glif --video 640 480 -f fonts/SFNSMono.ttf --v4l2 /dev/video10 --dark -s 2
```

**macOS / Windows:** Use `--pipe-ppm` or `--pipe-raw` piped through ffmpeg to OBS Virtual Camera. See [docs/webcam.md](docs/webcam.md) for full setup instructions on all platforms.

**Web demo:** Works everywhere — `make wasm && cd web && python3 -m http.server 8000`, open in browser, click Webcam, share the browser tab.

## Web Demo

The C core compiles to WebAssembly via Emscripten. The web frontend uses a WebGL font-atlas shader for GPU-accelerated rendering (with Canvas 2D fallback).

```bash
make wasm
cd web && python3 -m http.server 8000
```

Supports drag-and-drop images, video/GIF playback, and live webcam — all processed client-side in the browser.

## Examples

```bash
# High-res, high character count — small cells = more detail
./glif photo.png -f fonts/SFNSMono.ttf -w 4 -h 8 -d 2.0 -g 2.0

# Same, with PPM image output (scale 4 for crisp glyphs)
./glif photo.png -f fonts/SFNSMono.ttf -w 4 -h 8 -d 2.0 -g 2.0 -o out.ppm -s 4

# Modern terminal (120×40) with ANSI truecolor
./glif photo.png -f fonts/SFNSMono.ttf -w 9 -h 18 -c

# Auto-fit to current terminal size
./glif photo.png -f fonts/SFNSMono.ttf -a -c

# Classic 80×24 terminal
./glif photo.png -f fonts/SFNSMono.ttf -w 14 -h 31

# Cranked contrast for line art / high-contrast images
./glif diagram.png -f fonts/SFNSMono.ttf -d 3.0 -g 3.0

# Different fonts change the output character — experiment!
./glif photo.png -f fonts/GeistMono-Regular.ttf -w 6 -h 12 -c
```

Cell size controls the resolution/detail trade-off: smaller cells = more characters = finer detail. Directional crunch (`-d`) sharpens edges; global crunch (`-g`) increases overall contrast. Both default to moderate values — push them to 2.0–3.0 for sharper results.

## Performance

The pipeline is optimized for real-time throughput via three techniques:

1. **sRGB LUT** — 256-entry lookup table replaces per-pixel `powf()` in lightness computation (21x speedup)
2. **Precomputed circle masks** — Index-offset tables built once at startup eliminate per-pixel distance tests and `floorf`/`ceilf` calls. Interior cells skip bounds checking entirely (4.4x speedup)
3. **OpenMP parallel + SIMD** — All pipeline stages run across cores with `#pragma omp parallel for`; inner accumulation loops use `#pragma omp simd reduction` (2x+ speedup)

Benchmarks on Apple M3 Pro (11 cores), compiled with `-O2`:

| Image | Resolution | Cells | Per-frame | FPS |
|-------|-----------|-------|-----------|-----|
| raccoon.jpg | 679×679 | 2,211 (33×67) | 0.69 ms | 1,440 |
| wildboar.jpg | 1100×731 | 3,960 (55×36) | 0.91 ms | 1,096 |

Per-frame includes: lightness map, grid vector computation, color averaging, directional + global contrast, and character matching. One-time costs (image loading, font/character DB, mask precomputation) are excluded.

Build and run the benchmark tool:

```bash
make tools/bench
./tools/bench images/raccoon.jpg -f fonts/SFNSMono.ttf
```

## Testing

```bash
make test
```

## Attribution

This project implements the ASCII rendering technique described by [Alex Harri](https://alexharri.com) in his article **["Rendering ASCII art from images"](https://alexharri.com/blog/ascii-rendering)**. The core insight — representing characters as multi-dimensional shape vectors sampled from overlapping circles rather than scalar brightness values — comes directly from that article.

## License

MIT
