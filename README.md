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
