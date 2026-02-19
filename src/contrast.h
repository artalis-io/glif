#ifndef GLIF_CONTRAST_H
#define GLIF_CONTRAST_H

#include "grid.h"
#include "sampling.h"

/* Adaptive contrast parameters.
 * When floor >= 0, per-cell crunch is adjusted based on cell luminance
 * relative to the frame's brightness. Call glif_contrast_analyze_frame() each
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
    /* Per-frame stats (set by glif_contrast_analyze_frame) */
    float frame_avg;  /* average cell luminance this frame */
    float frame_p10;  /* 10th percentile luminance */
    float frame_p90;  /* 90th percentile luminance */
} GlifAdaptiveContrast;

/* Compute per-frame luminance statistics from grid cells.
 * Must be called after glif_grid_compute_colors() and before contrast passes.
 * Populates frame_avg, frame_p10, frame_p90 in ac. */
void glif_contrast_analyze_frame(GlifAdaptiveContrast *ac, const GlifGrid *grid);

/* Apply directional contrast enhancement using external sampling circles.
 * For each internal circle, compute how different it is from its neighboring
 * external circles, then exponentiate to crunch contrast. */
void glif_contrast_directional(GlifGrid *grid, const GlifSamplingConfig *sc,
                          float crunch, const GlifAdaptiveContrast *ac);

/* Apply global contrast enhancement: normalize each cell's shape vector
 * by its max component, then exponentiate. */
void glif_contrast_global(GlifGrid *grid, float crunch, const GlifAdaptiveContrast *ac);

#endif
