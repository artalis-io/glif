#include "temporal.h"
#include "quickselect.h"
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#if defined(__EMSCRIPTEN__) && defined(__wasm_simd128__)
#include <wasm_simd128.h>
#define USE_WASM_SIMD 1
#endif

#define quickselect glif_quickselect

/* ── A. GlifNormSmoother ── */

void glif_norm_smoother_init(GlifNormSmoother *ns) {
    ns->smooth_p5 = 0.0f;
    ns->smooth_p95 = 0.0f;
    ns->ready = 0;
}

void glif_norm_smoother_apply(GlifNormSmoother *ns, GlifLightnessMap *lm, float alpha) {
    size_t n = (size_t)lm->width * (size_t)lm->height;
    if (n < 2 || n > (size_t)INT_MAX) return;

    /* Compute frame percentiles via quickselect on a scratch copy */
    if (n > SIZE_MAX / sizeof(float)) return;
    float *scratch = malloc(n * sizeof(float));
    if (!scratch) return;
    memcpy(scratch, lm->data, n * sizeof(float));

    float p5  = quickselect(scratch, (int)n, (int)(n / 20));
    float p95 = quickselect(scratch, (int)n, (int)(n - 1 - n / 20));
    free(scratch);

    /* EMA blend */
    if (!ns->ready) {
        ns->smooth_p5 = p5;
        ns->smooth_p95 = p95;
        ns->ready = 1;
    } else {
        float beta = 1.0f - alpha;
        ns->smooth_p5 = alpha * p5 + beta * ns->smooth_p5;
        ns->smooth_p95 = alpha * p95 + beta * ns->smooth_p95;
    }

    /* Normalize using smoothed percentiles */
    float range = ns->smooth_p95 - ns->smooth_p5;
    if (range < 1e-4f) return;

    float inv_range = 1.0f / range;
#if USE_WASM_SIMD
    {
        v128_t vp5 = wasm_f32x4_splat(ns->smooth_p5);
        v128_t vinv = wasm_f32x4_splat(inv_range);
        v128_t vzero = wasm_f32x4_splat(0.0f);
        v128_t vone = wasm_f32x4_splat(1.0f);
        size_t i = 0, n4 = n & ~(size_t)3;
        for (; i < n4; i += 4) {
            v128_t v = wasm_v128_load(lm->data + i);
            v = wasm_f32x4_mul(wasm_f32x4_sub(v, vp5), vinv);
            v = wasm_f32x4_max(v, vzero);
            v = wasm_f32x4_min(v, vone);
            wasm_v128_store(lm->data + i, v);
        }
        for (; i < n; i++) {
            float v = (lm->data[i] - ns->smooth_p5) * inv_range;
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            lm->data[i] = v;
        }
    }
#else
    for (size_t i = 0; i < n; i++) {
        float v = (lm->data[i] - ns->smooth_p5) * inv_range;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        lm->data[i] = v;
    }
#endif
}

/* ── B. GlifShapeSmoother ── */

void glif_shape_smoother_init(GlifShapeSmoother *ss) {
    ss->prev_shapes = NULL;
    ss->prev_externals = NULL;
    ss->count = 0;
    ss->ready = 0;
}

void glif_shape_smoother_apply(GlifShapeSmoother *ss, GlifGrid *grid, float alpha) {
    int n = grid->rows * grid->cols;

    /* Reset on grid size change */
    if (n != ss->count) {
        free(ss->prev_shapes);
        free(ss->prev_externals);
        ss->prev_shapes = calloc((size_t)n, sizeof(GlifVec6));
        ss->prev_externals = calloc((size_t)n, sizeof(GlifVec10));
        if (!ss->prev_shapes || !ss->prev_externals) {
            free(ss->prev_shapes);
            free(ss->prev_externals);
            ss->prev_shapes = NULL;
            ss->prev_externals = NULL;
            ss->count = 0;
            ss->ready = 0;
            return;
        }
        ss->count = n;
        ss->ready = 0;
    }

    if (!ss->ready) {
        /* First frame: store current vectors, don't modify */
        for (int i = 0; i < n; i++) {
            ss->prev_shapes[i] = grid->cells[i].shape;
            ss->prev_externals[i] = grid->cells[i].external;
        }
        ss->ready = 1;
        return;
    }

    /* Blend current with previous via EMA */
    float beta = 1.0f - alpha;
    for (int i = 0; i < n; i++) {
        GlifVec6 blended = glif_vec6_add(glif_vec6_scale(grid->cells[i].shape, alpha),
                                glif_vec6_scale(ss->prev_shapes[i], beta));
        grid->cells[i].shape = blended;
        ss->prev_shapes[i] = blended;

#if USE_WASM_SIMD
        {
            v128_t va = wasm_f32x4_splat(alpha);
            v128_t vb = wasm_f32x4_splat(beta);
            /* First 4 floats */
            v128_t cur0 = wasm_v128_load(grid->cells[i].external.v);
            v128_t prev0 = wasm_v128_load(ss->prev_externals[i].v);
            v128_t blend0 = wasm_f32x4_add(wasm_f32x4_mul(cur0, va),
                                            wasm_f32x4_mul(prev0, vb));
            wasm_v128_store(grid->cells[i].external.v, blend0);
            wasm_v128_store(ss->prev_externals[i].v, blend0);
            /* Next 4 floats */
            v128_t cur1 = wasm_v128_load(grid->cells[i].external.v + 4);
            v128_t prev1 = wasm_v128_load(ss->prev_externals[i].v + 4);
            v128_t blend1 = wasm_f32x4_add(wasm_f32x4_mul(cur1, va),
                                            wasm_f32x4_mul(prev1, vb));
            wasm_v128_store(grid->cells[i].external.v + 4, blend1);
            wasm_v128_store(ss->prev_externals[i].v + 4, blend1);
            /* Tail: 2 scalar floats */
            float bv8 = alpha * grid->cells[i].external.v[8]
                      + beta * ss->prev_externals[i].v[8];
            grid->cells[i].external.v[8] = bv8;
            ss->prev_externals[i].v[8] = bv8;
            float bv9 = alpha * grid->cells[i].external.v[9]
                      + beta * ss->prev_externals[i].v[9];
            grid->cells[i].external.v[9] = bv9;
            ss->prev_externals[i].v[9] = bv9;
        }
#else
        for (int j = 0; j < 10; j++) {
            float bv = alpha * grid->cells[i].external.v[j]
                     + beta * ss->prev_externals[i].v[j];
            grid->cells[i].external.v[j] = bv;
            ss->prev_externals[i].v[j] = bv;
        }
#endif
    }
}

void glif_shape_smoother_free(GlifShapeSmoother *ss) {
    free(ss->prev_shapes);
    free(ss->prev_externals);
    ss->prev_shapes = NULL;
    ss->prev_externals = NULL;
    ss->count = 0;
    ss->ready = 0;
}

/* ── C. GlifContrastSmoother ── */

void glif_contrast_smoother_init(GlifContrastSmoother *cs) {
    cs->smooth_p10 = 0.0f;
    cs->smooth_p90 = 0.0f;
    cs->smooth_avg = 0.0f;
    cs->ready = 0;
}

void glif_contrast_smoother_apply(GlifContrastSmoother *cs, GlifAdaptiveContrast *ac,
                             float alpha) {
    if (!cs->ready) {
        cs->smooth_p10 = ac->frame_p10;
        cs->smooth_p90 = ac->frame_p90;
        cs->smooth_avg = ac->frame_avg;
        cs->ready = 1;
    } else {
        float beta = 1.0f - alpha;
        cs->smooth_p10 = alpha * ac->frame_p10 + beta * cs->smooth_p10;
        cs->smooth_p90 = alpha * ac->frame_p90 + beta * cs->smooth_p90;
        cs->smooth_avg = alpha * ac->frame_avg + beta * cs->smooth_avg;
    }

    /* Write smoothed values back */
    ac->frame_p10 = cs->smooth_p10;
    ac->frame_p90 = cs->smooth_p90;
    ac->frame_avg = cs->smooth_avg;
}

/* ── D. GlifMatchSmoother ── */

void glif_match_smoother_init(GlifMatchSmoother *ms) {
    ms->prev_chars = NULL;
    ms->count = 0;
    ms->ready = 0;
}

void glif_match_smoother_apply(GlifMatchSmoother *ms, GlifGrid *grid,
                          const GlifCharDatabase *db, float hysteresis) {
    int n = grid->rows * grid->cols;

    /* Reset on grid size change */
    if (n != ms->count) {
        free(ms->prev_chars);
        ms->prev_chars = calloc((size_t)n, 1);
        if (!ms->prev_chars) {
            ms->count = 0;
            ms->ready = 0;
            glif_match_grid(grid, db);
            return;
        }
        ms->count = n;
        ms->ready = 0;
    }

    if (!ms->ready) {
        /* First frame: normal best-match */
        glif_match_grid(grid, db);
        for (int i = 0; i < n; i++)
            ms->prev_chars[i] = grid->cells[i].ch;
        ms->ready = 1;
        return;
    }

    /* Find best match per cell with hysteresis */
    float threshold = 1.0f - hysteresis;
    GlifMatchCache mc;
    if (glif_match_cache_init(&mc) != 0) {
        mc.cache = NULL;
        mc.cache_size = 0;
    }

    for (int i = 0; i < n; i++) {
        char new_ch = glif_match_find(&grid->cells[i].shape, db, &mc);
        char prev_ch = ms->prev_chars[i];

        if (new_ch == prev_ch) {
            grid->cells[i].ch = prev_ch;
            continue;
        }

        int prev_idx = prev_ch - GLIF_CHAR_FIRST;
        if (prev_idx < 0 || prev_idx >= GLIF_CHAR_COUNT) {
            grid->cells[i].ch = new_ch;
            ms->prev_chars[i] = new_ch;
            continue;
        }

        float dist_new = glif_vec6_dist_sq(grid->cells[i].shape,
                                      db->entries[new_ch - GLIF_CHAR_FIRST].shape);
        float dist_prev = glif_vec6_dist_sq(grid->cells[i].shape,
                                       db->entries[prev_idx].shape);

        if (dist_new < dist_prev * threshold) {
            grid->cells[i].ch = new_ch;
            ms->prev_chars[i] = new_ch;
        } else {
            grid->cells[i].ch = prev_ch;
        }
    }

    glif_match_cache_free(&mc);
}

void glif_match_smoother_free(GlifMatchSmoother *ms) {
    free(ms->prev_chars);
    ms->prev_chars = NULL;
    ms->count = 0;
    ms->ready = 0;
}
