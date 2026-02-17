#include "contrast.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static float smoothstep(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static float cell_luminance(const GridCell *cell) {
    return (cell->r * 0.2126f + cell->g * 0.7152f + cell->b * 0.0722f)
           / 255.0f;
}

static int float_cmp(const void *a, const void *b) {
    float fa = *(const float *)a, fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

void contrast_analyze_frame(AdaptiveContrast *ac, const Grid *grid) {
    if (!ac || ac->floor < 0.0f) return;

    int n = grid->rows * grid->cols;
    if (n == 0) return;

    float *lums = malloc((size_t)n * sizeof(float));
    if (!lums) return;

    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        lums[i] = cell_luminance(&grid->cells[i]);
        sum += lums[i];
    }
    ac->frame_avg = sum / (float)n;

    qsort(lums, (size_t)n, sizeof(float), float_cmp);
    ac->frame_p10 = lums[n / 10];
    ac->frame_p90 = lums[n - 1 - n / 10];

    free(lums);
}

/* Compute effective crunch for a cell based on its brightness
 * relative to the frame's luminance distribution.
 * Returns base crunch when adaptive is disabled (ac == NULL or floor < 0). */
static float effective_crunch(float crunch, const GridCell *cell,
                              const AdaptiveContrast *ac) {
    if (!ac || ac->floor < 0.0f) return crunch;

    float lum = cell_luminance(cell);

    /* Map user-specified floor/ceil into frame-relative thresholds.
     * The base floor/ceil are scaled by the frame's brightness so that
     * a dark frame shifts thresholds down and a bright frame shifts them up. */
    float range = ac->frame_p90 - ac->frame_p10;
    float eff_floor, eff_ceil;
    if (range > 0.01f) {
        /* Place floor/ceil relative to the frame's actual luminance spread */
        eff_floor = ac->frame_p10 + ac->floor * range;
        eff_ceil  = ac->frame_p10 + ac->ceil  * range;
    } else {
        /* Near-uniform frame: fall back to absolute thresholds */
        eff_floor = ac->floor;
        eff_ceil  = ac->ceil;
    }

    if (lum < eff_floor) return crunch;  /* below noise floor: full crush */

    float t = smoothstep(eff_floor, eff_ceil, lum);
    return 1.0f + (crunch - 1.0f) * t;
}

void contrast_directional(Grid *grid, const SamplingConfig *sc,
                          float crunch, const AdaptiveContrast *ac) {
    int ncells = grid->rows * grid->cols;

#ifdef _OPENMP
    #pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < ncells; i++) {
        GridCell *cell = &grid->cells[i];
        float c = effective_crunch(crunch, cell, ac);

        /* For each internal circle, find max of (internal, affecting externals),
         * then apply: value = (value / maxValue)^crunch * maxValue */
        for (int s = 0; s < NUM_INTERNAL; s++) {
            float val = cell->shape.v[s];
            float max_val = val;

            for (int a = 0; a < sc->affecting_count[s]; a++) {
                int ext_idx = sc->affecting[s][a];
                if (cell->external.v[ext_idx] > max_val)
                    max_val = cell->external.v[ext_idx];
            }

            if (max_val > 1e-8f) {
                cell->shape.v[s] = powf(val / max_val, c) * max_val;
            }
        }
    }
}

void contrast_global(Grid *grid, float crunch, const AdaptiveContrast *ac) {
    int ncells = grid->rows * grid->cols;

#ifdef _OPENMP
    #pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < ncells; i++) {
        GridCell *cell = &grid->cells[i];
        float c = effective_crunch(crunch, cell, ac);

        float max_c = vec6_max_component(cell->shape);
        if (max_c > 1e-8f) {
            for (int s = 0; s < NUM_INTERNAL; s++) {
                float val = cell->shape.v[s];
                cell->shape.v[s] = powf(val / max_c, c) * max_c;
            }
        }
    }
}
