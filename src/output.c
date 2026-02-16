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

int output_ppm(const Grid *grid, const CharDatabase *db,
               const char *path, int scale) {
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
                    dst[0] = (uint8_t)(cell->r * alpha);
                    dst[1] = (uint8_t)(cell->g * alpha);
                    dst[2] = (uint8_t)(cell->b * alpha);
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
