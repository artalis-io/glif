#include "sampling.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void glif_sampling_config_init(GlifSamplingConfig *sc) {
    /* 6 internal circles in 3×2 staggered grid */
    float ix[] = {0.25f, 0.75f, 0.25f, 0.75f, 0.25f, 0.75f};
    float iy[] = {0.20f, 0.13f, 0.53f, 0.47f, 0.87f, 0.80f};
    float ir = 0.28f;

    for (int i = 0; i < GLIF_NUM_INTERNAL; i++) {
        sc->internal[i].cx = ix[i];
        sc->internal[i].cy = iy[i];
        sc->internal[i].r  = ir;
    }

    /*
     * 10 external circles arranged around the cell perimeter.
     * Layout (roughly): 3 above, 2 left, 2 right, 3 below
     *   Indices: 0-2 top, 3-4 left, 5-6 right, 7-9 bottom
     */
    float ex[] = {0.00f, 0.50f, 1.00f,  -0.25f, -0.25f,  1.25f, 1.25f,  0.00f, 0.50f, 1.00f};
    float ey[] = {-0.17f, -0.17f, -0.17f,  0.33f, 0.67f,  0.33f, 0.67f,  1.17f, 1.17f, 1.17f};
    float er = 0.28f;

    for (int i = 0; i < GLIF_NUM_EXTERNAL; i++) {
        sc->external[i].cx = ex[i];
        sc->external[i].cy = ey[i];
        sc->external[i].r  = er;
    }

    /*
     * Affecting table: which external circles influence each internal circle.
     * Matches the article's AFFECTING_EXTERNAL_INDICES.
     *
     * Internal 0 (top-left):    ext 0(top-left), 1(top-mid), 2(top-right), 4(left-lower)
     * Internal 1 (top-right):   ext 0(top-left), 1(top-mid), 3(left-upper), 5(right-upper)
     * Internal 2 (mid-left):    ext 2(top-right), 4(left-lower), 6(right-lower)
     * Internal 3 (mid-right):   ext 3(left-upper), 5(right-upper), 7(bot-left)
     * Internal 4 (bot-left):    ext 4(left-lower), 6(right-lower), 8(bot-mid), 9(bot-right)
     * Internal 5 (bot-right):   ext 5(right-upper), 7(bot-left), 8(bot-mid), 9(bot-right)
     */
    int aff[GLIF_NUM_INTERNAL][GLIF_MAX_AFFECTING] = {
        {0, 1, 2, 4},
        {0, 1, 3, 5},
        {2, 4, 6, -1},
        {3, 5, 7, -1},
        {4, 6, 8, 9},
        {5, 7, 8, 9},
    };
    int aff_count[] = {4, 4, 3, 3, 4, 4};

    for (int i = 0; i < GLIF_NUM_INTERNAL; i++) {
        sc->affecting_count[i] = aff_count[i];
        for (int j = 0; j < GLIF_MAX_AFFECTING; j++)
            sc->affecting[i][j] = aff[i][j];
    }
}

float glif_sampling_circle_average(const GlifLightnessMap *lm,
                              float cx_px, float cy_px, float r_px) {
    int x0 = (int)floorf(cx_px - r_px);
    int y0 = (int)floorf(cy_px - r_px);
    int x1 = (int)ceilf(cx_px + r_px);
    int y1 = (int)ceilf(cy_px + r_px);

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= lm->width) x1 = lm->width - 1;
    if (y1 >= lm->height) y1 = lm->height - 1;

    float r2 = r_px * r_px;
    float sum = 0;
    int count = 0;

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - cx_px;
            float dy = (float)y - cy_px;
            if (dx * dx + dy * dy <= r2) {
                sum += lm->data[y * lm->width + x];
                count++;
            }
        }
    }

    return count > 0 ? sum / (float)count : 0.0f;
}

/* Build a GlifCircleMask for one circle: precompute all (dx, dy) pixel offsets
 * relative to cell origin (0,0) that fall within the circle, then encode
 * as row-major index offsets (dy * stride + dx) for direct lm->data access. */
static int build_mask(GlifCircleMask *mask, float cx_norm, float cy_norm,
                      float r_norm, int cell_w, int cell_h, int stride) {
    float cx_px = cx_norm * (float)cell_w;
    float cy_px = cy_norm * (float)cell_h;
    float r_px  = r_norm * (float)cell_w;
    float r2 = r_px * r_px;

    int x0 = (int)floorf(cx_px - r_px);
    int y0 = (int)floorf(cy_px - r_px);
    int x1 = (int)ceilf(cx_px + r_px);
    int y1 = (int)ceilf(cy_px + r_px);

    /* First pass: count pixels */
    int count = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - cx_px;
            float dy = (float)y - cy_px;
            if (dx * dx + dy * dy <= r2) count++;
        }
    }
    if (count == 0) {
        mask->offsets = NULL;
        mask->count = 0;
        mask->inv_count = 0.0f;
        mask->min_dx = mask->max_dx = mask->min_dy = mask->max_dy = 0;
        return 0;
    }

    mask->offsets = malloc((size_t)count * sizeof(int));
    if (!mask->offsets) return -1;
    mask->count = count;
    mask->inv_count = 1.0f / (float)count;
    mask->min_dx = x0;
    mask->max_dx = x1;
    mask->min_dy = y0;
    mask->max_dy = y1;

    /* Second pass: store offsets */
    int k = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - cx_px;
            float dy = (float)y - cy_px;
            if (dx * dx + dy * dy <= r2) {
                mask->offsets[k++] = y * stride + x;
            }
        }
    }
    return 0;
}

int glif_sampling_precompute(GlifPrecomputedMasks *pm, const GlifSamplingConfig *sc,
                        int cell_w, int cell_h, int stride) {
    memset(pm, 0, sizeof(*pm));
    pm->stride = stride;

    for (int i = 0; i < GLIF_NUM_INTERNAL; i++) {
        if (build_mask(&pm->masks[i],
                       sc->internal[i].cx, sc->internal[i].cy, sc->internal[i].r,
                       cell_w, cell_h, stride) != 0)
            goto fail;
    }
    for (int i = 0; i < GLIF_NUM_EXTERNAL; i++) {
        if (build_mask(&pm->masks[GLIF_NUM_INTERNAL + i],
                       sc->external[i].cx, sc->external[i].cy, sc->external[i].r,
                       cell_w, cell_h, stride) != 0)
            goto fail;
    }
    return 0;

fail:
    glif_sampling_precompute_free(pm);
    return -1;
}

void glif_sampling_precompute_free(GlifPrecomputedMasks *pm) {
    for (int i = 0; i < GLIF_NUM_CIRCLES; i++)
        free(pm->masks[i].offsets);
    memset(pm, 0, sizeof(*pm));
}
