#ifndef OUTPUT_H
#define OUTPUT_H

#include "grid.h"
#include "font.h"

/* Print plain ASCII to stdout. */
void output_plain(const Grid *grid);

/* Print ANSI truecolor ASCII to stdout. */
void output_ansi(const Grid *grid);

/* Write PPM image file with colored glyph rendering.
 * scale > 1 uses high-res render bitmaps for sharp text. */
int output_ppm(const Grid *grid, const CharDatabase *db,
               const char *path, int scale);

#endif
