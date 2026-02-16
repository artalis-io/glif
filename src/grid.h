#ifndef GRID_H
#define GRID_H

#include "vec6.h"
#include "image.h"
#include "sampling.h"
#include <stdint.h>

typedef struct {
    int px;          /* top-left pixel x */
    int py;          /* top-left pixel y */
    Vec6 shape;      /* 6D internal shape vector */
    Vec10 external;  /* 10D external shape vector */
    uint8_t r, g, b; /* average color */
    char ch;         /* matched ASCII character */
} GridCell;

typedef struct {
    int rows;
    int cols;
    int cell_w;
    int cell_h;
    GridCell *cells;
} Grid;

/* Create grid by dividing image into cells. */
int grid_create(Grid *grid, const Image *img, int cell_w, int cell_h);

/* Compute Vec6 internal + Vec10 external vectors for each cell. */
void grid_compute_vectors(Grid *grid, const LightnessMap *lm,
                          const SamplingConfig *sc);

/* Fast path using precomputed circle masks. */
void grid_compute_vectors_fast(Grid *grid, const LightnessMap *lm,
                               const PrecomputedMasks *pm);

/* Compute average RGB color per cell. */
void grid_compute_colors(Grid *grid, const Image *img);

void grid_free(Grid *grid);

#endif
