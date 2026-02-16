#ifndef OUTPUT_H
#define OUTPUT_H

#include "grid.h"
#include "font.h"

/* Print plain ASCII to stdout. */
void output_plain(const Grid *grid);

/* Print ANSI truecolor ASCII to stdout. */
void output_ansi(const Grid *grid);

/* Print ANSI truecolor with cursor home (for video mode — overwrites in-place). */
void output_ansi_inplace(const Grid *grid);

/* Write PPM image file with glyph rendering.
 * scale > 1 uses high-res render bitmaps for sharp text.
 * dark_mode: 0 = color background + white glyphs (default)
 *            1 = black background + colored glyphs */
int output_ppm(const Grid *grid, const CharDatabase *db,
               const char *path, int scale, int dark_mode);

#endif
