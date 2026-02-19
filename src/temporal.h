#ifndef GLIF_TEMPORAL_H
#define GLIF_TEMPORAL_H

#include "image.h"
#include "grid.h"
#include "contrast.h"
#include "match.h"
#include "font.h"

/* ── A. GlifNormSmoother — Smoothed normalization percentiles ── */

typedef struct {
    float smooth_p5, smooth_p95;
    int ready;
} GlifNormSmoother;

void glif_norm_smoother_init(GlifNormSmoother *ns);
void glif_norm_smoother_apply(GlifNormSmoother *ns, GlifLightnessMap *lm, float alpha);

/* ── B. GlifShapeSmoother — Temporal EMA on shape + external vectors ── */

typedef struct {
    GlifVec6 *prev_shapes;
    GlifVec10 *prev_externals;
    int count;
    int ready;
} GlifShapeSmoother;

void glif_shape_smoother_init(GlifShapeSmoother *ss);
void glif_shape_smoother_apply(GlifShapeSmoother *ss, GlifGrid *grid, float alpha);
void glif_shape_smoother_free(GlifShapeSmoother *ss);

/* ── C. GlifContrastSmoother — Smoothed adaptive contrast stats ── */

typedef struct {
    float smooth_p10, smooth_p90, smooth_avg;
    int ready;
} GlifContrastSmoother;

void glif_contrast_smoother_init(GlifContrastSmoother *cs);
void glif_contrast_smoother_apply(GlifContrastSmoother *cs, GlifAdaptiveContrast *ac,
                             float alpha);

/* ── D. GlifMatchSmoother — Character hysteresis ── */

typedef struct {
    char *prev_chars;
    int count;
    int ready;
} GlifMatchSmoother;

void glif_match_smoother_init(GlifMatchSmoother *ms);
void glif_match_smoother_apply(GlifMatchSmoother *ms, GlifGrid *grid,
                          const GlifCharDatabase *db, float hysteresis);
void glif_match_smoother_free(GlifMatchSmoother *ms);

#endif
