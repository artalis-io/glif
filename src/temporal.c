#include "temporal.h"
#include <stdlib.h>
#include <string.h>

/* ── Quickselect (copied from contrast.c — static there) ── */

static float quickselect(float *arr, int n, int k) {
    int lo = 0, hi = n - 1;
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

/* ── A. NormSmoother ── */

void norm_smoother_init(NormSmoother *ns) {
    ns->smooth_p5 = 0.0f;
    ns->smooth_p95 = 0.0f;
    ns->ready = 0;
}

void norm_smoother_apply(NormSmoother *ns, LightnessMap *lm, float alpha) {
    size_t n = (size_t)lm->width * (size_t)lm->height;
    if (n < 2) return;

    /* Compute frame percentiles via quickselect on a scratch copy */
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

/* ── B. ShapeSmoother ── */

void shape_smoother_init(ShapeSmoother *ss) {
    ss->prev_shapes = NULL;
    ss->prev_externals = NULL;
    ss->count = 0;
    ss->ready = 0;
}

void shape_smoother_apply(ShapeSmoother *ss, Grid *grid, float alpha) {
    int n = grid->rows * grid->cols;

    /* Reset on grid size change */
    if (n != ss->count) {
        free(ss->prev_shapes);
        free(ss->prev_externals);
        ss->prev_shapes = malloc((size_t)n * sizeof(Vec6));
        ss->prev_externals = malloc((size_t)n * sizeof(Vec10));
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
        Vec6 blended = vec6_add(vec6_scale(grid->cells[i].shape, alpha),
                                vec6_scale(ss->prev_shapes[i], beta));
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

void shape_smoother_free(ShapeSmoother *ss) {
    free(ss->prev_shapes);
    free(ss->prev_externals);
    ss->prev_shapes = NULL;
    ss->prev_externals = NULL;
    ss->count = 0;
    ss->ready = 0;
}

/* ── C. ContrastSmoother ── */

void contrast_smoother_init(ContrastSmoother *cs) {
    cs->smooth_p10 = 0.0f;
    cs->smooth_p90 = 0.0f;
    cs->smooth_avg = 0.0f;
    cs->ready = 0;
}

void contrast_smoother_apply(ContrastSmoother *cs, AdaptiveContrast *ac,
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

/* ── D. MatchSmoother ── */

void match_smoother_init(MatchSmoother *ms) {
    ms->prev_chars = NULL;
    ms->count = 0;
    ms->ready = 0;
}

void match_smoother_apply(MatchSmoother *ms, Grid *grid,
                          const CharDatabase *db, float hysteresis) {
    int n = grid->rows * grid->cols;

    /* Reset on grid size change */
    if (n != ms->count) {
        free(ms->prev_chars);
        ms->prev_chars = calloc((size_t)n, 1);
        if (!ms->prev_chars) {
            ms->count = 0;
            ms->ready = 0;
            match_grid(grid, db);
            return;
        }
        ms->count = n;
        ms->ready = 0;
    }

    if (!ms->ready) {
        /* First frame: normal best-match */
        match_grid(grid, db);
        for (int i = 0; i < n; i++)
            ms->prev_chars[i] = grid->cells[i].ch;
        ms->ready = 1;
        return;
    }

    /* Find best match per cell with hysteresis */
    float threshold = 1.0f - hysteresis;
    MatchCache mc;
    if (match_cache_init(&mc) != 0) {
        mc.cache = NULL;
        mc.cache_size = 0;
    }

    for (int i = 0; i < n; i++) {
        char new_ch = match_find(&grid->cells[i].shape, db, &mc);
        char prev_ch = ms->prev_chars[i];

        if (new_ch == prev_ch) {
            grid->cells[i].ch = prev_ch;
            continue;
        }

        int prev_idx = prev_ch - CHAR_FIRST;
        if (prev_idx < 0 || prev_idx >= CHAR_COUNT) {
            grid->cells[i].ch = new_ch;
            ms->prev_chars[i] = new_ch;
            continue;
        }

        float dist_new = vec6_dist_sq(grid->cells[i].shape,
                                      db->entries[new_ch - CHAR_FIRST].shape);
        float dist_prev = vec6_dist_sq(grid->cells[i].shape,
                                       db->entries[prev_idx].shape);

        if (dist_new < dist_prev * threshold) {
            grid->cells[i].ch = new_ch;
            ms->prev_chars[i] = new_ch;
        } else {
            grid->cells[i].ch = prev_ch;
        }
    }

    match_cache_free(&mc);
}

void match_smoother_free(MatchSmoother *ms) {
    free(ms->prev_chars);
    ms->prev_chars = NULL;
    ms->count = 0;
    ms->ready = 0;
}
