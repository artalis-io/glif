"""Render an image to ASCII art and print to the terminal.

Usage:
    python -m examples.render <image> <font> [options]

Examples:
    python -m examples.render ../../images/raccoon.jpg ../../fonts/GeistMono-Regular.ttf
    python -m examples.render ../../images/raccoon.jpg ../../fonts/GeistMono-Regular.ttf --color
    python -m examples.render ../../images/raccoon.jpg ../../fonts/GeistMono-Regular.ttf --color --cell-width 8 --cell-height 16
"""

import argparse
import sys

import glif


def main():
    parser = argparse.ArgumentParser(description="Render an image to ASCII art")
    parser.add_argument("image", help="Path to input image (PNG, JPEG, etc.)")
    parser.add_argument("font", help="Path to monospace TTF font")
    parser.add_argument("--color", action="store_true", help="ANSI truecolor output")
    parser.add_argument("--cell-width", type=int, default=10, help="Cell width in pixels (default: 10)")
    parser.add_argument("--cell-height", type=int, default=20, help="Cell height in pixels (default: 20)")
    parser.add_argument("--dir-crunch", type=float, default=1.25, help="Directional contrast exponent (default: 1.25)")
    parser.add_argument("--global-crunch", type=float, default=1.5, help="Global contrast exponent (default: 1.5)")
    args = parser.parse_args()

    result = glif.render(
        args.image, args.font,
        cell_width=args.cell_width,
        cell_height=args.cell_height,
        dir_crunch=args.dir_crunch,
        global_crunch=args.global_crunch,
    )

    if args.color:
        sys.stdout.write(result.to_ansi())
    else:
        print(result.to_plain_text())

    print(f"\n{result.cols}x{result.rows} grid", file=sys.stderr)


if __name__ == "__main__":
    main()
