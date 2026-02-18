# Glif — Development Guide

## Build

```bash
make          # build glif binary
make clean    # remove build artifacts
make test     # build and run unit tests
```

## Project structure

- `src/` — All source code. Each module is a `.h/.c` pair.
- `vendor/` — Vendored single-header libraries (stb_image.h, stb_truetype.h, utest.h). Do not modify.
- `fonts/` — Monospace TTF fonts for testing. Not committed to git (user-provided).
- `tests/` — Unit tests using Sheredom's utest.h framework.
- `images/` — Test PNG images.

## Architecture

The rendering pipeline processes an image through these stages in order:

1. Load image → compute per-pixel lightness (sRGB→linear luminance)
2. Divide image into grid cells (default 10×20px)
3. Sample 6 internal circles per cell → Vec6 shape vector
4. Sample 10 external circles per cell → Vec10 for contrast
5. Directional contrast: diff internal vs. neighboring externals, normalize, exponentiate
6. Global contrast: normalize by max component, exponentiate
7. Match each cell's Vec6 to nearest character Vec6 (Euclidean distance)
8. Output: plain ASCII, ANSI truecolor, or PPM image

Key insight: characters are 6D shape vectors (not scalar brightness), enabling sharp contour matching.

## Conventions

- C11, compiled with `-Wall -Wextra -Wpedantic`
- No dynamic allocations without corresponding free
- All public functions prefixed with their module name (e.g. `grid_create`, `image_load`)
- Header-only code in `vec6.h` uses `static inline`
- stb libraries: `#define STB_*_IMPLEMENTATION` in exactly one .c file

## Testing

Tests live in `tests/test_*.c`. Each test file is a standalone executable.
Run all tests with `make test`.
