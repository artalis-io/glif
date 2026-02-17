#ifndef CONTRAST_H
#define CONTRAST_H

#include "grid.h"
#include "sampling.h"

/* Adaptive contrast parameters.
 * When floor >= 0, per-cell crunch is adjusted based on cell luminance
 * relative to the frame's brightness. Call contrast_analyze_frame() each
 * frame to compute per-frame stats before running contrast passes.
 *
 * Per-cell behavior (using frame-relative thresholds):
 *   - Below effective floor: full crunch (crushes noise)
 *   - floor → ceil: smoothstep from crunch=1.0 up to full crunch
 *   - Above ceil: full crunch
 * Set floor < 0 to disable (fixed crunch for all cells). */
typedef struct {
    float floor;  /* base noise floor (0.0–1.0), <0 = disabled */
    float ceil;   /* base ceiling (0.0–1.0) */
    /* Per-frame stats (set by contrast_analyze_frame) */
    float frame_avg;  /* average cell luminance this frame */
    float frame_p10;  /* 10th percentile luminance */
    float frame_p90;  /* 90th percentile luminance */
} AdaptiveContrast;

/* Compute per-frame luminance statistics from grid cells.
 * Must be called after grid_compute_colors() and before contrast passes.
 * Populates frame_avg, frame_p10, frame_p90 in ac. */
void contrast_analyze_frame(AdaptiveContrast *ac, const Grid *grid);

/* Apply directional contrast enhancement using external sampling circles.
 * For each internal circle, compute how different it is from its neighboring
 * external circles, then exponentiate to crunch contrast. */
void contrast_directional(Grid *grid, const SamplingConfig *sc,
                          float crunch, const AdaptiveContrast *ac);

/* Apply global contrast enhancement: normalize each cell's shape vector
 * by its max component, then exponentiate. */
void contrast_global(Grid *grid, float crunch, const AdaptiveContrast *ac);

#endif
