#ifndef SAMPLING_H
#define SAMPLING_H

#include "vec6.h"
#include "image.h"

#define NUM_INTERNAL 6
#define NUM_EXTERNAL 10
#define MAX_AFFECTING 4

typedef struct {
    float cx; /* center x, normalized 0–1 within cell */
    float cy; /* center y, normalized 0–1 within cell */
    float r;  /* radius, normalized to cell width */
} SamplingCircle;

typedef struct {
    SamplingCircle internal[NUM_INTERNAL];
    SamplingCircle external[NUM_EXTERNAL];
    /* For each internal circle: which external circles affect it */
    int affecting[NUM_INTERNAL][MAX_AFFECTING];
    int affecting_count[NUM_INTERNAL];
} SamplingConfig;

/* Initialize the standard sampling circle layout. */
void sampling_config_init(SamplingConfig *sc);

/* Average lightness within a circle centered at pixel (cx_px, cy_px) with radius r_px. */
float sampling_circle_average(const LightnessMap *lm, float cx_px, float cy_px, float r_px);

/* Precomputed circle mask: array of row-major index offsets from cell origin */
typedef struct {
    int *offsets;    /* index offsets: dy * stride + dx for each pixel in circle */
    int count;       /* number of pixels in circle */
    float inv_count; /* 1.0f / count */
    int min_dx;      /* bounding box of offsets for edge detection */
    int max_dx;
    int min_dy;
    int max_dy;
} CircleMask;

#define NUM_CIRCLES (NUM_INTERNAL + NUM_EXTERNAL)

typedef struct {
    CircleMask masks[NUM_CIRCLES]; /* 0..5 = internal, 6..15 = external */
    int stride;                     /* lm->width (row stride) */
} PrecomputedMasks;

/* Precompute circle pixel masks for given cell size and image stride. */
int sampling_precompute(PrecomputedMasks *pm, const SamplingConfig *sc,
                        int cell_w, int cell_h, int stride);
void sampling_precompute_free(PrecomputedMasks *pm);

#endif
