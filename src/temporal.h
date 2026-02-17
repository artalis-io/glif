#ifndef TEMPORAL_H
#define TEMPORAL_H

#include "image.h"
#include "grid.h"
#include "contrast.h"
#include "match.h"
#include "font.h"

/* ── A. NormSmoother — Smoothed normalization percentiles ── */

typedef struct {
    float smooth_p5, smooth_p95;
    int ready;
} NormSmoother;

void norm_smoother_init(NormSmoother *ns);
void norm_smoother_apply(NormSmoother *ns, LightnessMap *lm, float alpha);

/* ── B. ShapeSmoother — Temporal EMA on shape + external vectors ── */

typedef struct {
    Vec6 *prev_shapes;
    Vec10 *prev_externals;
    int count;
    int ready;
} ShapeSmoother;

void shape_smoother_init(ShapeSmoother *ss);
void shape_smoother_apply(ShapeSmoother *ss, Grid *grid, float alpha);
void shape_smoother_free(ShapeSmoother *ss);

/* ── C. ContrastSmoother — Smoothed adaptive contrast stats ── */

typedef struct {
    float smooth_p10, smooth_p90, smooth_avg;
    int ready;
} ContrastSmoother;

void contrast_smoother_init(ContrastSmoother *cs);
void contrast_smoother_apply(ContrastSmoother *cs, AdaptiveContrast *ac,
                             float alpha);

/* ── D. MatchSmoother — Character hysteresis ── */

typedef struct {
    char *prev_chars;
    int count;
    int ready;
} MatchSmoother;

void match_smoother_init(MatchSmoother *ms);
void match_smoother_apply(MatchSmoother *ms, Grid *grid,
                          const CharDatabase *db, float hysteresis);
void match_smoother_free(MatchSmoother *ms);

#endif
