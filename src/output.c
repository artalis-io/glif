#include "output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void output_plain(const Grid *grid) {
    for (int r = 0; r < grid->rows; r++) {
        for (int c = 0; c < grid->cols; c++) {
            putchar(grid->cells[r * grid->cols + c].ch);
        }
        putchar('\n');
    }
}

void output_ansi(const Grid *grid) {
    for (int r = 0; r < grid->rows; r++) {
        for (int c = 0; c < grid->cols; c++) {
            const GridCell *cell = &grid->cells[r * grid->cols + c];
            printf("\033[38;2;%d;%d;%dm%c",
                   cell->r, cell->g, cell->b, cell->ch);
        }
        printf("\033[0m\n");
    }
    printf("\033[0m");
}

int frame_diff_init(FrameDiff *fd, int rows, int cols) {
    int cells = rows * cols;
    /* Worst case: every cell changed, ~48 bytes per cell + cursor moves */
    fd->bufsize = (size_t)cells * 60 + (size_t)rows * 6 + 16;
    fd->buf = malloc(fd->bufsize);
    if (!fd->buf) return -1;
    fd->prev = calloc((size_t)cells, 4);  /* zeroed — first frame renders all */
    if (!fd->prev) { free(fd->buf); fd->buf = NULL; return -1; }
    fd->cells = cells;
    fd->cols = cols;
    return 0;
}

/* Full redraw — cursor home, emit every cell sequentially. No cursor moves needed. */
static void frame_full_redraw(FrameDiff *fd, const Grid *grid, int dark_mode) {
    char *p = fd->buf;
    int rows = grid->rows;
    int cols = grid->cols;

    *p++ = '\033'; *p++ = '['; *p++ = 'H';

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            const GridCell *cell = &grid->cells[r * cols + c];
            if (dark_mode) {
                p += sprintf(p, "\033[38;2;%d;%d;%dm%c",
                             cell->r, cell->g, cell->b, cell->ch);
            } else {
                p += sprintf(p, "\033[48;2;%d;%d;%dm\033[38;2;255;255;255m%c",
                             cell->r, cell->g, cell->b, cell->ch);
            }
        }
        *p++ = '\033'; *p++ = '['; *p++ = '0'; *p++ = 'm'; *p++ = '\n';
    }
    *p++ = '\033'; *p++ = '['; *p++ = '0'; *p++ = 'm';

    fwrite(fd->buf, 1, (size_t)(p - fd->buf), stdout);
    fflush(stdout);
}

void frame_diff_render(FrameDiff *fd, const Grid *grid, int dark_mode) {
    int rows = grid->rows;
    int cols = grid->cols;
    int total = rows * cols;

    /* Estimate byte cost of diff vs full redraw.
     * cell_cost: ~22 bytes (dark) or ~46 bytes (light) per cell
     * full: cursor_home(3) + total * cell_cost + rows * reset_newline(5)
     * diff: changed * cell_cost + jumps * cursor_move(~9) + reset(4)
     * A jump is needed when a changed cell isn't consecutive to the previous. */
    int cell_cost = dark_mode ? 22 : 46;
    int changed = 0, jumps = 0;
    int prev_idx = -2;  /* impossible value so first changed cell counts as jump */
    for (int i = 0; i < total; i++) {
        const GridCell *cell = &grid->cells[i];
        uint8_t *pv = &fd->prev[i * 4];
        if (pv[0] != (uint8_t)cell->ch ||
            pv[1] != cell->r ||
            pv[2] != cell->g ||
            pv[3] != cell->b) {
            changed++;
            if (i != prev_idx + 1) jumps++;
            prev_idx = i;
        }
    }

    int full_cost = 3 + total * cell_cost + rows * 5;
    int diff_cost = changed * cell_cost + jumps * 9 + 4;
    if (diff_cost >= full_cost) {
        /* Update prev buffer */
        for (int i = 0; i < total; i++) {
            const GridCell *cell = &grid->cells[i];
            uint8_t *prev = &fd->prev[i * 4];
            prev[0] = (uint8_t)cell->ch;
            prev[1] = cell->r;
            prev[2] = cell->g;
            prev[3] = cell->b;
        }
        frame_full_redraw(fd, grid, dark_mode);
        return;
    }

    /* Diff path — only emit changed cells */
    char *p = fd->buf;
    int cursor_r = -1, cursor_c = -1;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = r * cols + c;
            const GridCell *cell = &grid->cells[idx];
            uint8_t *prev = &fd->prev[idx * 4];

            if (prev[0] == (uint8_t)cell->ch &&
                prev[1] == cell->r &&
                prev[2] == cell->g &&
                prev[3] == cell->b) {
                continue;
            }

            prev[0] = (uint8_t)cell->ch;
            prev[1] = cell->r;
            prev[2] = cell->g;
            prev[3] = cell->b;

            if (cursor_r != r || cursor_c != c) {
                p += sprintf(p, "\033[%d;%dH", r + 1, c + 1);
            }

            if (dark_mode) {
                p += sprintf(p, "\033[38;2;%d;%d;%dm%c",
                             cell->r, cell->g, cell->b, cell->ch);
            } else {
                p += sprintf(p, "\033[48;2;%d;%d;%dm\033[38;2;255;255;255m%c",
                             cell->r, cell->g, cell->b, cell->ch);
            }
            cursor_r = r;
            cursor_c = c + 1;
        }
    }

    if (p > fd->buf) {
        p += sprintf(p, "\033[0m");
        fwrite(fd->buf, 1, (size_t)(p - fd->buf), stdout);
        fflush(stdout);
    }
}

void frame_diff_free(FrameDiff *fd) {
    free(fd->buf);
    free(fd->prev);
    fd->buf = NULL;
    fd->prev = NULL;
}

int output_ppm(const Grid *grid, const CharDatabase *db,
               const char *path, int scale, int dark_mode) {
    if (!grid || !grid->cells || !db || !path) return -1;
    if (scale < 1) scale = 1;
    if (scale > 64) scale = 64;
    int rw = grid->cell_w * scale;  /* render cell width */
    int rh = grid->cell_h * scale;  /* render cell height */
    size_t img_w = (size_t)grid->cols * (size_t)rw;
    size_t img_h = (size_t)grid->rows * (size_t)rh;

    /* Get high-res glyph bitmaps if scaled, otherwise use the analysis bitmaps */
    uint8_t **render_bmps = NULL;
    if (scale > 1) {
        render_bmps = char_db_render_bitmaps(db, scale);
        if (!render_bmps) return -1;
    }

    uint8_t *pixels = calloc(img_w * img_h * 3, 1);
    if (!pixels) {
        if (render_bmps) {
            for (int i = 0; i < CHAR_COUNT; i++) free(render_bmps[i]);
            free(render_bmps);
        }
        return -1;
    }

    for (int r = 0; r < grid->rows; r++) {
        for (int c = 0; c < grid->cols; c++) {
            const GridCell *cell = &grid->cells[r * grid->cols + c];

            int char_idx = cell->ch - CHAR_FIRST;
            if (char_idx < 0 || char_idx >= CHAR_COUNT) char_idx = 0;

            const uint8_t *bmp;
            int bw, bh;
            if (render_bmps) {
                bmp = render_bmps[char_idx];
                bw = rw;
                bh = rh;
            } else {
                bmp = db->entries[char_idx].bitmap;
                bw = grid->cell_w;
                bh = grid->cell_h;
            }

            int ox = c * rw;
            int oy = r * rh;

            for (int y = 0; y < bh; y++) {
                for (int x = 0; x < bw; x++) {
                    size_t px = (size_t)(ox + x);
                    size_t py = (size_t)(oy + y);
                    if (px >= img_w || py >= img_h) continue;

                    float alpha = bmp ? bmp[y * bw + x] / 255.0f : 0.0f;
                    uint8_t *dst = pixels + (py * img_w + px) * 3;
                    if (dark_mode) {
                        /* Black background, colored glyphs */
                        dst[0] = (uint8_t)(cell->r * alpha);
                        dst[1] = (uint8_t)(cell->g * alpha);
                        dst[2] = (uint8_t)(cell->b * alpha);
                    } else {
                        /* Color background, white glyphs */
                        dst[0] = (uint8_t)(cell->r + (255 - cell->r) * alpha);
                        dst[1] = (uint8_t)(cell->g + (255 - cell->g) * alpha);
                        dst[2] = (uint8_t)(cell->b + (255 - cell->b) * alpha);
                    }
                }
            }
        }
    }

    if (render_bmps) {
        for (int i = 0; i < CHAR_COUNT; i++) free(render_bmps[i]);
        free(render_bmps);
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(pixels);
        fprintf(stderr, "error: cannot open '%s' for writing\n", path);
        return -1;
    }

    fprintf(f, "P6\n%zu %zu\n255\n", img_w, img_h);
    size_t expected = img_w * img_h * 3;
    size_t written = fwrite(pixels, 1, expected, f);
    fclose(f);
    free(pixels);
    if (written != expected) return -1;
    return 0;
}

int ppm_pipe_init(PpmPipe *pp, const Grid *grid, const CharDatabase *db,
                  int scale, int dark_mode) {
    if (scale < 1) scale = 1;
    if (scale > 64) scale = 64;
    pp->scale = scale;
    pp->dark_mode = dark_mode;
    pp->rw = grid->cell_w * scale;
    pp->rh = grid->cell_h * scale;
    pp->img_w = (size_t)grid->cols * (size_t)pp->rw;
    pp->img_h = (size_t)grid->rows * (size_t)pp->rh;

    pp->pixels = malloc(pp->img_w * pp->img_h * 3);
    if (!pp->pixels) return -1;

    pp->render_bmps = NULL;
    if (scale > 1) {
        pp->render_bmps = char_db_render_bitmaps(db, scale);
        if (!pp->render_bmps) { free(pp->pixels); pp->pixels = NULL; return -1; }
    }
    return 0;
}

void ppm_pipe_frame(PpmPipe *pp, const Grid *grid, const CharDatabase *db) {
    int rw = pp->rw, rh = pp->rh;
    size_t img_w = pp->img_w;
    int dark_mode = pp->dark_mode;
    uint8_t *pixels = pp->pixels;

    for (int r = 0; r < grid->rows; r++) {
        for (int c = 0; c < grid->cols; c++) {
            const GridCell *cell = &grid->cells[r * grid->cols + c];
            int char_idx = cell->ch - CHAR_FIRST;
            if (char_idx < 0 || char_idx >= CHAR_COUNT) char_idx = 0;

            const uint8_t *bmp;
            int bw, bh;
            if (pp->render_bmps) {
                bmp = pp->render_bmps[char_idx];
                bw = rw; bh = rh;
            } else {
                bmp = db->entries[char_idx].bitmap;
                bw = grid->cell_w; bh = grid->cell_h;
            }

            int ox = c * rw, oy = r * rh;
            for (int y = 0; y < bh; y++) {
                uint8_t *row = pixels + ((size_t)(oy + y) * img_w + (size_t)ox) * 3;
                for (int x = 0; x < bw; x++) {
                    float alpha = bmp ? bmp[y * bw + x] / 255.0f : 0.0f;
                    if (dark_mode) {
                        row[0] = (uint8_t)(cell->r * alpha);
                        row[1] = (uint8_t)(cell->g * alpha);
                        row[2] = (uint8_t)(cell->b * alpha);
                    } else {
                        row[0] = (uint8_t)(cell->r + (255 - cell->r) * alpha);
                        row[1] = (uint8_t)(cell->g + (255 - cell->g) * alpha);
                        row[2] = (uint8_t)(cell->b + (255 - cell->b) * alpha);
                    }
                    row += 3;
                }
            }
        }
    }

    /* Write PPM header + pixels to stdout */
    fprintf(stdout, "P6\n%zu %zu\n255\n", pp->img_w, pp->img_h);
    fwrite(pixels, 1, pp->img_w * pp->img_h * 3, stdout);
    fflush(stdout);
}

void ppm_pipe_free(PpmPipe *pp) {
    free(pp->pixels);
    if (pp->render_bmps) {
        for (int i = 0; i < CHAR_COUNT; i++) free(pp->render_bmps[i]);
        free(pp->render_bmps);
    }
    pp->pixels = NULL;
    pp->render_bmps = NULL;
}
