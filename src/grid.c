#include "grid.h"
#include <stdlib.h>

int grid_create(Grid *grid, const Image *img, int cell_w, int cell_h) {
    grid->cell_w = cell_w;
    grid->cell_h = cell_h;
    grid->cols = img->width / cell_w;
    grid->rows = img->height / cell_h;
    if (grid->cols == 0 || grid->rows == 0) return -1;

    grid->cells = calloc((size_t)grid->rows * (size_t)grid->cols,
                         sizeof(GridCell));
    if (!grid->cells) return -1;

    for (int r = 0; r < grid->rows; r++) {
        for (int c = 0; c < grid->cols; c++) {
            GridCell *cell = &grid->cells[r * grid->cols + c];
            cell->px = c * cell_w;
            cell->py = r * cell_h;
            cell->ch = ' ';
        }
    }
    return 0;
}

void grid_compute_vectors(Grid *grid, const LightnessMap *lm,
                          const SamplingConfig *sc) {
    float cw = (float)grid->cell_w;
    float ch = (float)grid->cell_h;

    for (int r = 0; r < grid->rows; r++) {
        for (int c = 0; c < grid->cols; c++) {
            GridCell *cell = &grid->cells[r * grid->cols + c];
            float ox = (float)cell->px;
            float oy = (float)cell->py;

            /* Internal circles */
            for (int i = 0; i < NUM_INTERNAL; i++) {
                float cx_px = ox + sc->internal[i].cx * cw;
                float cy_px = oy + sc->internal[i].cy * ch;
                float r_px  = sc->internal[i].r * cw;
                cell->shape.v[i] = sampling_circle_average(lm, cx_px, cy_px, r_px);
            }

            /* External circles */
            for (int i = 0; i < NUM_EXTERNAL; i++) {
                float cx_px = ox + sc->external[i].cx * cw;
                float cy_px = oy + sc->external[i].cy * ch;
                float r_px  = sc->external[i].r * cw;
                cell->external.v[i] = sampling_circle_average(lm, cx_px, cy_px, r_px);
            }
        }
    }
}

void grid_compute_vectors_fast(Grid *grid, const LightnessMap *lm,
                               const PrecomputedMasks *pm) {
    const float *data = lm->data;
    int img_w = lm->width;
    int img_h = lm->height;

    /* Compute safe bounds: cells where ALL circle offsets are in-bounds */
    int global_min_dx = 0, global_max_dx = 0;
    int global_min_dy = 0, global_max_dy = 0;
    for (int i = 0; i < NUM_CIRCLES; i++) {
        const CircleMask *m = &pm->masks[i];
        if (m->count == 0) continue;
        if (m->min_dx < global_min_dx) global_min_dx = m->min_dx;
        if (m->max_dx > global_max_dx) global_max_dx = m->max_dx;
        if (m->min_dy < global_min_dy) global_min_dy = m->min_dy;
        if (m->max_dy > global_max_dy) global_max_dy = m->max_dy;
    }

    /* Cell (px, py) is safe when:
     *   px + global_min_dx >= 0  &&  px + global_max_dx < img_w
     *   py + global_min_dy >= 0  &&  py + global_max_dy < img_h */
    int safe_px_min = -global_min_dx;
    int safe_px_max = img_w - 1 - global_max_dx;
    int safe_py_min = -global_min_dy;
    int safe_py_max = img_h - 1 - global_max_dy;

    int ncells = grid->rows * grid->cols;
    GridCell *cells = grid->cells;
    const CircleMask *masks = pm->masks;

    #pragma omp parallel for schedule(static)
    for (int ci = 0; ci < ncells; ci++) {
        GridCell *cell = &cells[ci];
        int px = cell->px;
        int py = cell->py;
        int base = py * img_w + px;

        int safe = (px >= safe_px_min && px <= safe_px_max &&
                    py >= safe_py_min && py <= safe_py_max);

        if (safe) {
            for (int i = 0; i < NUM_INTERNAL; i++) {
                const int *offs = masks[i].offsets;
                int cnt = masks[i].count;
                float sum = 0;
                #pragma omp simd reduction(+:sum)
                for (int k = 0; k < cnt; k++)
                    sum += data[base + offs[k]];
                cell->shape.v[i] = sum * masks[i].inv_count;
            }
            for (int i = 0; i < NUM_EXTERNAL; i++) {
                const int *offs = masks[NUM_INTERNAL + i].offsets;
                int cnt = masks[NUM_INTERNAL + i].count;
                float sum = 0;
                #pragma omp simd reduction(+:sum)
                for (int k = 0; k < cnt; k++)
                    sum += data[base + offs[k]];
                cell->external.v[i] = sum * masks[NUM_INTERNAL + i].inv_count;
            }
        } else {
            for (int i = 0; i < NUM_INTERNAL; i++) {
                const int *offs = masks[i].offsets;
                int cnt = masks[i].count;
                float sum = 0;
                int valid = 0;
                for (int k = 0; k < cnt; k++) {
                    int idx = base + offs[k];
                    int y = idx / img_w;
                    int x = idx % img_w;
                    if (idx >= 0 && y < img_h && x >= 0 && x < img_w) {
                        sum += data[idx];
                        valid++;
                    }
                }
                cell->shape.v[i] = valid > 0 ? sum / (float)valid : 0.0f;
            }
            for (int i = 0; i < NUM_EXTERNAL; i++) {
                const int *offs = masks[NUM_INTERNAL + i].offsets;
                int cnt = masks[NUM_INTERNAL + i].count;
                float sum = 0;
                int valid = 0;
                for (int k = 0; k < cnt; k++) {
                    int idx = base + offs[k];
                    int y = idx / img_w;
                    int x = idx % img_w;
                    if (idx >= 0 && y < img_h && x >= 0 && x < img_w) {
                        sum += data[idx];
                        valid++;
                    }
                }
                cell->external.v[i] = valid > 0 ? sum / (float)valid : 0.0f;
            }
        }
    }
}

void grid_compute_colors(Grid *grid, const Image *img) {
    int ncells = grid->rows * grid->cols;

    #pragma omp parallel for schedule(static)
    for (int ci = 0; ci < ncells; ci++) {
            GridCell *cell = &grid->cells[ci];
            int rsum = 0, gsum = 0, bsum = 0, count = 0;

            for (int y = cell->py; y < cell->py + grid->cell_h && y < img->height; y++) {
                for (int x = cell->px; x < cell->px + grid->cell_w && x < img->width; x++) {
                    const uint8_t *px = img->pixels + (y * img->width + x) * img->channels;
                    rsum += px[0];
                    gsum += px[1];
                    bsum += px[2];
                    count++;
                }
            }
            if (count > 0) {
                cell->r = (uint8_t)(rsum / count);
                cell->g = (uint8_t)(gsum / count);
                cell->b = (uint8_t)(bsum / count);
            }
    }
}

void grid_free(Grid *grid) {
    if (!grid) return;
    free(grid->cells);
    grid->cells = NULL;
}
