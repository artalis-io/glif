"""Demonstrates the low-level pipeline API with step-by-step rendering.

Usage:
    python -m examples.pipeline <image> <font>

Example:
    python -m examples.pipeline ../../images/raccoon.jpg ../../fonts/GeistMono-Regular.ttf
"""

import argparse
import sys

import glif


def main():
    parser = argparse.ArgumentParser(description="Low-level pipeline example")
    parser.add_argument("image", help="Path to input image")
    parser.add_argument("font", help="Path to monospace TTF font")
    args = parser.parse_args()

    cell_w, cell_h = 10, 20

    with glif.Image(args.image) as img:
        print(f"Loaded image: {img.width}x{img.height} ({img.channels} channels)", file=sys.stderr)

        with glif.LightnessMap(img) as lm:
            print(f"Lightness map: {lm.width}x{lm.height}", file=sys.stderr)

            sc = glif.SamplingConfig()
            with glif.PrecomputedMasks(sc, cell_w, cell_h, lm.width) as pm:
                with glif.CharDatabase(args.font, cell_w, cell_h, sc) as db:
                    with glif.Grid(img, cell_w, cell_h) as grid:
                        print(f"Grid: {grid.cols}x{grid.rows} cells", file=sys.stderr)

                        grid.compute_vectors_fast(lm, pm)
                        grid.compute_colors(img)
                        grid.apply_directional_contrast(sc, 1.25)
                        grid.apply_global_contrast(1.5)
                        grid.match(db)

                        cells = grid.cells()
                        print(f"Matched {len(cells)} cells", file=sys.stderr)

                        # Print ANSI output
                        for r in range(grid.rows):
                            offset = r * grid.cols
                            for c in range(grid.cols):
                                cell = cells[offset + c]
                                sys.stdout.write(
                                    f"\033[38;2;{cell.r};{cell.g};{cell.b}m{cell.ch}"
                                )
                            sys.stdout.write("\033[0m\n")


if __name__ == "__main__":
    main()
