#include "temporal.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* ── Quickselect (copied from contrast.c — static there) ── */

/* NaN values are moved to the end to prevent infinite loops. */
static float quickselect(float *arr, int n, int k) {
    /* Filter NaN to end */
    int valid = n;
    for (int i = 0; i < valid; ) {
        if (arr[i] != arr[i]) { /* NaN */
            valid--;
            arr[i] = arr[valid];
        } else {
            i++;
        }
    }
    if (valid == 0) return 0.0f;
    if (k >= valid) k = valid - 1;

    int lo = 0, hi = valid - 1;
    while (lo < hi) {
        float pivot = arr[lo + (hi - lo) / 2];
        int i = lo, j = hi;
        while (i <= j) {
            while (arr[i] < pivot) i++;
            while (arr[j] > pivot) j--;
            if (i <= j) {
                float tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
                i++; j--;
            }
        }
        if (k <= j) hi = j;
        else if (k >= i) lo = i;
        else break;
    }
    return arr[k];
}

/* ── A. GlifNormSmoother ── */

void glif_norm_smoother_init(GlifNormSmoother *ns) {
    ns->smooth_p5 = 0.0f;
    ns->smooth_p95 = 0.0f;
    ns->ready = 0;
}

void glif_norm_smoother_apply(GlifNormSmoother *ns, GlifLightnessMap *lm, float alpha) {
    size_t n = (size_t)lm->width * (size_t)lm->height;
    if (n < 2) return;

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
    for (size_t i = 0; i < n; i++) {
        float v = (lm->data[i] - ns->smooth_p5) * inv_range;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        lm->data[i] = v;
    }
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

        for (int j = 0; j < 10; j++) {
            float bv = alpha * grid->cells[i].external.v[j]
                     + beta * ss->prev_externals[i].v[j];
            grid->cells[i].external.v[j] = bv;
            ss->prev_externals[i].v[j] = bv;
        }
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
