#include "contrast.h"
#include "quickselect.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__) && defined(__wasm_simd128__)
#include <wasm_simd128.h>
#define USE_WASM_SIMD 1
#endif

static float smoothstep(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static float cell_luminance(const GlifGridCell *cell) {
    return (cell->r * 0.2126f + cell->g * 0.7152f + cell->b * 0.0722f)
           / 255.0f;
}

#define quickselect glif_quickselect

void glif_contrast_analyze_frame(GlifAdaptiveContrast *ac, const GlifGrid *grid) {
    if (!ac || ac->floor < 0.0f) return;

    int n = grid->rows * grid->cols;
    if (n == 0) return;

    float *lums = malloc((size_t)n * sizeof(float));
    if (!lums) return;

    float sum = 0.0f;
    const GlifGridCell *cells = grid->cells;

#ifdef _OPENMP
    #pragma omp parallel for reduction(+:sum) schedule(static)
#endif
#if USE_WASM_SIMD
    {
        v128_t wr = wasm_f32x4_splat(0.2126f / 255.0f);
        v128_t wg = wasm_f32x4_splat(0.7152f / 255.0f);
        v128_t wb = wasm_f32x4_splat(0.0722f / 255.0f);
        v128_t vsum = wasm_f32x4_splat(0.0f);
        int i = 0, n4 = n & ~3;
        for (; i < n4; i += 4) {
            v128_t r = wasm_f32x4_make((float)cells[i].r, (float)cells[i+1].r,
                                       (float)cells[i+2].r, (float)cells[i+3].r);
            v128_t g = wasm_f32x4_make((float)cells[i].g, (float)cells[i+1].g,
                                       (float)cells[i+2].g, (float)cells[i+3].g);
            v128_t b = wasm_f32x4_make((float)cells[i].b, (float)cells[i+1].b,
                                       (float)cells[i+2].b, (float)cells[i+3].b);
            v128_t lum = wasm_f32x4_add(wasm_f32x4_add(wasm_f32x4_mul(r, wr),
                                                         wasm_f32x4_mul(g, wg)),
                                         wasm_f32x4_mul(b, wb));
            wasm_v128_store(lums + i, lum);
            vsum = wasm_f32x4_add(vsum, lum);
        }
        float tmp[4]; wasm_v128_store(tmp, vsum);
        sum = tmp[0] + tmp[1] + tmp[2] + tmp[3];
        for (; i < n; i++) {
            float l = cell_luminance(&cells[i]);
            lums[i] = l;
            sum += l;
        }
    }
#else
    for (int i = 0; i < n; i++) {
        float l = cell_luminance(&cells[i]);
        lums[i] = l;
        sum += l;
    }
#endif
    ac->frame_avg = sum / (float)n;

    /* Quickselect is O(n) vs qsort's O(n log n) for percentiles.
     * Find p10 first (partially partitions), then p90. */
    ac->frame_p10 = quickselect(lums, n, n / 10);
    ac->frame_p90 = quickselect(lums, n, n - 1 - n / 10);

    free(lums);
}

/* Compute effective crunch for a cell based on its brightness
 * relative to the frame's luminance distribution.
 * Returns base crunch when adaptive is disabled (ac == NULL or floor < 0). */
static float effective_crunch(float crunch, const GlifGridCell *cell,
                              const GlifAdaptiveContrast *ac) {
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

void glif_contrast_directional(GlifGrid *grid, const GlifSamplingConfig *sc,
                          float crunch, const GlifAdaptiveContrast *ac) {
    int ncells = grid->rows * grid->cols;

#ifdef _OPENMP
    #pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < ncells; i++) {
        GlifGridCell *cell = &grid->cells[i];
        float c = effective_crunch(crunch, cell, ac);

        /* For each internal circle, find max of (internal, affecting externals),
         * then apply: value = (value / maxValue)^crunch * maxValue */
        for (int s = 0; s < GLIF_NUM_INTERNAL; s++) {
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

void glif_contrast_global(GlifGrid *grid, float crunch, const GlifAdaptiveContrast *ac) {
    int ncells = grid->rows * grid->cols;

#ifdef _OPENMP
    #pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < ncells; i++) {
        GlifGridCell *cell = &grid->cells[i];
        float c = effective_crunch(crunch, cell, ac);

        float max_c = glif_vec6_max_component(cell->shape);
        if (max_c > 1e-8f) {
            for (int s = 0; s < GLIF_NUM_INTERNAL; s++) {
                float val = cell->shape.v[s];
                cell->shape.v[s] = powf(val / max_c, c) * max_c;
            }
        }
    }
}
