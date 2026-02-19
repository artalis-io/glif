#ifndef GLIF_GRID_H
#define GLIF_GRID_H

#include "vec6.h"
#include "image.h"
#include "sampling.h"
#include <stdint.h>

typedef struct {
    int px;          /* top-left pixel x */
    int py;          /* top-left pixel y */
    GlifVec6 shape;      /* 6D internal shape vector */
    GlifVec10 external;  /* 10D external shape vector */
    uint8_t r, g, b; /* average color */
    char ch;         /* matched ASCII character */
} GlifGridCell;

typedef struct {
    int rows;
    int cols;
    int cell_w;
    int cell_h;
    GlifGridCell *cells;
} GlifGrid;

/* Create grid by dividing image into cells. */
int glif_grid_create(GlifGrid *grid, const GlifImage *img, int cell_w, int cell_h);

/* Compute GlifVec6 internal + GlifVec10 external vectors for each cell. */
void glif_grid_compute_vectors(GlifGrid *grid, const GlifLightnessMap *lm,
                          const GlifSamplingConfig *sc);

/* Fast path using precomputed circle masks. */
void glif_grid_compute_vectors_fast(GlifGrid *grid, const GlifLightnessMap *lm,
                               const GlifPrecomputedMasks *pm);

/* Compute average RGB color per cell. */
void glif_grid_compute_colors(GlifGrid *grid, const GlifImage *img);

void glif_grid_free(GlifGrid *grid);

#endif
