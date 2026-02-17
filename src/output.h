#ifndef OUTPUT_H
#define OUTPUT_H

#include "grid.h"
#include "font.h"
#include <stddef.h>

/* Print plain ASCII to stdout. */
void output_plain(const Grid *grid);

/* Print ANSI truecolor ASCII to stdout. */
void output_ansi(const Grid *grid);

/* Diff-based ANSI renderer for video mode — only emits changed cells.
 * dark_mode: 0 = color background + white glyphs
 *            1 = black background + colored glyphs */
typedef struct {
    char *buf;       /* write buffer (reused across frames) */
    size_t bufsize;
    uint8_t *prev;   /* previous frame: 4 bytes per cell [ch, r, g, b] */
    int cells;       /* total cell count */
    int cols;        /* grid columns (for cursor positioning) */
} FrameDiff;

int frame_diff_init(FrameDiff *fd, int rows, int cols);
void frame_diff_render(FrameDiff *fd, const Grid *grid, int dark_mode);
void frame_diff_free(FrameDiff *fd);

/* Write PPM image file with glyph rendering.
 * scale > 1 uses high-res render bitmaps for sharp text.
 * dark_mode: 0 = color background + white glyphs (default)
 *            1 = black background + colored glyphs */
int output_ppm(const Grid *grid, const CharDatabase *db,
               const char *path, int scale, int dark_mode);

/* Streaming PPM renderer for video mode — writes PPM frames to stdout.
 * Reuses allocated buffers across frames. */
typedef struct {
    uint8_t *pixels;      /* output pixel buffer */
    uint8_t **render_bmps; /* scaled glyph bitmaps (NULL if scale==1) */
    size_t img_w, img_h;  /* output image dimensions */
    int rw, rh;           /* render cell dimensions */
    int scale;
    int dark_mode;
} PpmPipe;

int ppm_pipe_init(PpmPipe *pp, const Grid *grid, const CharDatabase *db,
                  int scale, int dark_mode);
void ppm_pipe_frame(PpmPipe *pp, const Grid *grid, const CharDatabase *db);
void ppm_pipe_free(PpmPipe *pp);

#endif
