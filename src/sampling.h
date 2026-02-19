#ifndef GLIF_SAMPLING_H
#define GLIF_SAMPLING_H

#include "vec6.h"
#include "image.h"

#define GLIF_NUM_INTERNAL 6
#define GLIF_NUM_EXTERNAL 10
#define GLIF_MAX_AFFECTING 4

typedef struct {
    float cx; /* center x, normalized 0–1 within cell */
    float cy; /* center y, normalized 0–1 within cell */
    float r;  /* radius, normalized to cell width */
} GlifSamplingCircle;

typedef struct {
    GlifSamplingCircle internal[GLIF_NUM_INTERNAL];
    GlifSamplingCircle external[GLIF_NUM_EXTERNAL];
    /* For each internal circle: which external circles affect it */
    int affecting[GLIF_NUM_INTERNAL][GLIF_MAX_AFFECTING];
    int affecting_count[GLIF_NUM_INTERNAL];
} GlifSamplingConfig;

/* Initialize the standard sampling circle layout. */
void glif_sampling_config_init(GlifSamplingConfig *sc);

/* Average lightness within a circle centered at pixel (cx_px, cy_px) with radius r_px. */
float glif_sampling_circle_average(const GlifLightnessMap *lm, float cx_px, float cy_px, float r_px);

/* Precomputed circle mask: array of row-major index offsets from cell origin */
typedef struct {
    int *offsets;    /* index offsets: dy * stride + dx for each pixel in circle */
    int count;       /* number of pixels in circle */
    float inv_count; /* 1.0f / count */
    int min_dx;      /* bounding box of offsets for edge detection */
    int max_dx;
    int min_dy;
    int max_dy;
} GlifCircleMask;

#define GLIF_NUM_CIRCLES (GLIF_NUM_INTERNAL + GLIF_NUM_EXTERNAL)

typedef struct {
    GlifCircleMask masks[GLIF_NUM_CIRCLES]; /* 0..5 = internal, 6..15 = external */
    int stride;                     /* lm->width (row stride) */
} GlifPrecomputedMasks;

/* Precompute circle pixel masks for given cell size and image stride. */
int glif_sampling_precompute(GlifPrecomputedMasks *pm, const GlifSamplingConfig *sc,
                        int cell_w, int cell_h, int stride);
void glif_sampling_precompute_free(GlifPrecomputedMasks *pm);

#endif
