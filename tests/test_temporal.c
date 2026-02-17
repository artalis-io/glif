#include "utest.h"
#include "vec6.h"
#include "image.h"
#include "grid.h"
#include "contrast.h"
#include "match.h"
#include "font.h"
#include "temporal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Helpers ── */

static LightnessMap make_lm(int w, int h, const float *data) {
    LightnessMap lm;
    lm.width = w;
    lm.height = h;
    lm.data = malloc((size_t)(w * h) * sizeof(float));
    memcpy(lm.data, data, (size_t)(w * h) * sizeof(float));
    return lm;
}

static Grid make_grid(int rows, int cols) {
    Grid grid;
    grid.rows = rows;
    grid.cols = cols;
    grid.cell_w = 10;
    grid.cell_h = 20;
    grid.cells = calloc((size_t)(rows * cols), sizeof(GridCell));
    return grid;
}

/* Minimal CharDatabase with known shapes for match smoother tests.
 * Space and all chars except 'A','B' have zero shape.
 * 'A' = (1,0,0,0,0,0), 'B' = (0,1,0,0,0,0). */
static CharDatabase make_test_db(void) {
    CharDatabase db;
    memset(&db, 0, sizeof(db));
    db.cell_w = 10;
    db.cell_h = 20;
    for (int i = 0; i < CHAR_COUNT; i++) {
        db.entries[i].ch = (char)(CHAR_FIRST + i);
        db.entries[i].shape = vec6_zero();
    }
    db.entries['A' - CHAR_FIRST].shape = (Vec6){{1, 0, 0, 0, 0, 0}};
    db.entries['B' - CHAR_FIRST].shape = (Vec6){{0, 1, 0, 0, 0, 0}};
    return db;
}

/* ═══════════════════════════════════════════════
 *  A. NormSmoother tests
 * ═══════════════════════════════════════════════ */

UTEST(norm_smoother, first_frame_sets_percentiles) {
    /* 20 pixels with values 0.0 .. 0.95 (step 0.05) */
    float data[20];
    for (int i = 0; i < 20; i++) data[i] = (float)i * 0.05f;

    LightnessMap lm = make_lm(20, 1, data);
    NormSmoother ns;
    norm_smoother_init(&ns);

    norm_smoother_apply(&ns, &lm, 0.4f);

    /* p5  = data[1]  = 0.05
     * p95 = data[18] = 0.90 */
    ASSERT_NEAR(ns.smooth_p5,  0.05f, 1e-5f);
    ASSERT_NEAR(ns.smooth_p95, 0.90f, 1e-5f);
    ASSERT_TRUE(ns.ready);

    /* All output values in [0,1] */
    for (int i = 0; i < 20; i++) {
        ASSERT_GE(lm.data[i], 0.0f);
        ASSERT_LE(lm.data[i], 1.0f);
    }

    lightness_map_free(&lm);
}

UTEST(norm_smoother, second_frame_blends) {
    float data1[20], data2[20];
    for (int i = 0; i < 20; i++) data1[i] = (float)i * 0.05f;
    for (int i = 0; i < 20; i++) data2[i] = (float)i * 0.04f + 0.1f;

    LightnessMap lm1 = make_lm(20, 1, data1);
    NormSmoother ns;
    norm_smoother_init(&ns);

    /* Frame 1 */
    norm_smoother_apply(&ns, &lm1, 0.5f);
    float p5_after1 = ns.smooth_p5;
    float p95_after1 = ns.smooth_p95;
    lightness_map_free(&lm1);

    /* Frame 2 with different distribution */
    LightnessMap lm2 = make_lm(20, 1, data2);
    norm_smoother_apply(&ns, &lm2, 0.5f);

    /* Smoothed values should differ from both raw frame 2 percentiles
     * and the frame 1 values (they're blended) */
    /* data2 p5 = data2[1] = 0.14, p95 = data2[18] = 0.82 */
    float raw_p5 = 0.14f;
    ASSERT_NE(ns.smooth_p5, raw_p5);      /* not raw frame 2 */
    ASSERT_NE(ns.smooth_p5, p5_after1);   /* not pure frame 1 */
    (void)p95_after1;

    lightness_map_free(&lm2);
}

UTEST(norm_smoother, alpha1_matches_raw_normalize) {
    float data[20];
    for (int i = 0; i < 20; i++) data[i] = (float)i * 0.05f;

    /* Apply smoother with alpha=1 (two frames to test ongoing behavior) */
    NormSmoother ns;
    norm_smoother_init(&ns);

    LightnessMap lm_smooth = make_lm(20, 1, data);
    norm_smoother_apply(&ns, &lm_smooth, 1.0f);

    /* Apply raw normalize to identical data */
    LightnessMap lm_raw = make_lm(20, 1, data);
    lightness_map_normalize(&lm_raw);

    for (int i = 0; i < 20; i++) {
        ASSERT_NEAR(lm_smooth.data[i], lm_raw.data[i], 1e-5f);
    }

    lightness_map_free(&lm_smooth);
    lightness_map_free(&lm_raw);
}

UTEST(norm_smoother, convergence) {
    float data[20];
    for (int i = 0; i < 20; i++) data[i] = (float)i * 0.05f;

    NormSmoother ns;
    norm_smoother_init(&ns);

    /* Feed identical frames many times */
    for (int f = 0; f < 50; f++) {
        LightnessMap lm = make_lm(20, 1, data);
        norm_smoother_apply(&ns, &lm, 0.3f);
        lightness_map_free(&lm);
    }

    /* Should converge to raw percentiles */
    ASSERT_NEAR(ns.smooth_p5,  0.05f, 1e-3f);
    ASSERT_NEAR(ns.smooth_p95, 0.90f, 1e-3f);
}

/* ═══════════════════════════════════════════════
 *  B. ShapeSmoother tests
 * ═══════════════════════════════════════════════ */

UTEST(shape_smoother, first_frame_stores_not_modifies) {
    Grid grid = make_grid(1, 2);
    grid.cells[0].shape = (Vec6){{0.5f, 0.3f, 0.1f, 0.8f, 0.2f, 0.6f}};
    grid.cells[1].shape = (Vec6){{0.1f, 0.9f, 0.4f, 0.7f, 0.3f, 0.5f}};
    Vec6 orig0 = grid.cells[0].shape;
    Vec6 orig1 = grid.cells[1].shape;

    ShapeSmoother ss;
    shape_smoother_init(&ss);
    shape_smoother_apply(&ss, &grid, 0.4f);

    /* Vectors should be unchanged */
    for (int i = 0; i < 6; i++) {
        ASSERT_NEAR(grid.cells[0].shape.v[i], orig0.v[i], 1e-6f);
        ASSERT_NEAR(grid.cells[1].shape.v[i], orig1.v[i], 1e-6f);
    }

    shape_smoother_free(&ss);
    free(grid.cells);
}

UTEST(shape_smoother, second_frame_blends) {
    Grid grid = make_grid(1, 1);
    grid.cells[0].shape = (Vec6){{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    grid.cells[0].external = vec10_zero();

    ShapeSmoother ss;
    shape_smoother_init(&ss);
    shape_smoother_apply(&ss, &grid, 0.5f); /* frame 1: stored */

    /* Frame 2: different shape */
    grid.cells[0].shape = (Vec6){{0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    shape_smoother_apply(&ss, &grid, 0.5f);

    /* Result should be blend: 0.5 * current + 0.5 * prev */
    ASSERT_NEAR(grid.cells[0].shape.v[0], 0.5f, 1e-5f); /* 0.5*0 + 0.5*1 */
    ASSERT_NEAR(grid.cells[0].shape.v[1], 0.5f, 1e-5f); /* 0.5*1 + 0.5*0 */

    shape_smoother_free(&ss);
    free(grid.cells);
}

UTEST(shape_smoother, alpha1_no_smoothing) {
    Grid grid = make_grid(1, 1);
    grid.cells[0].shape = (Vec6){{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};

    ShapeSmoother ss;
    shape_smoother_init(&ss);
    shape_smoother_apply(&ss, &grid, 1.0f); /* frame 1 */

    /* Frame 2 */
    Vec6 frame2 = {{0.2f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f}};
    grid.cells[0].shape = frame2;
    shape_smoother_apply(&ss, &grid, 1.0f);

    /* alpha=1: result = 100% current frame */
    for (int i = 0; i < 6; i++) {
        ASSERT_NEAR(grid.cells[0].shape.v[i], frame2.v[i], 1e-5f);
    }

    shape_smoother_free(&ss);
    free(grid.cells);
}

UTEST(shape_smoother, grid_resize_resets) {
    Grid grid = make_grid(1, 2);
    grid.cells[0].shape = (Vec6){{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    grid.cells[1].shape = (Vec6){{0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f}};

    ShapeSmoother ss;
    shape_smoother_init(&ss);
    shape_smoother_apply(&ss, &grid, 0.5f); /* frame 1 */
    ASSERT_TRUE(ss.ready);
    free(grid.cells);

    /* Resize to different count */
    grid = make_grid(1, 3);
    grid.cells[0].shape = (Vec6){{0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f}};
    grid.cells[1].shape = (Vec6){{0.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f}};
    grid.cells[2].shape = (Vec6){{0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f}};
    Vec6 orig = grid.cells[0].shape;

    shape_smoother_apply(&ss, &grid, 0.5f);

    /* Should have reset and stored (not blended) */
    for (int i = 0; i < 6; i++) {
        ASSERT_NEAR(grid.cells[0].shape.v[i], orig.v[i], 1e-6f);
    }

    shape_smoother_free(&ss);
    free(grid.cells);
}

UTEST(shape_smoother, blends_both_vec6_vec10) {
    Grid grid = make_grid(1, 1);
    grid.cells[0].shape = (Vec6){{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    grid.cells[0].external = (Vec10){{1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};

    ShapeSmoother ss;
    shape_smoother_init(&ss);
    shape_smoother_apply(&ss, &grid, 0.5f); /* frame 1 */

    /* Frame 2 */
    grid.cells[0].shape = (Vec6){{0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    grid.cells[0].external = (Vec10){{0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    shape_smoother_apply(&ss, &grid, 0.5f);

    /* Vec6 blended */
    ASSERT_NEAR(grid.cells[0].shape.v[0], 0.5f, 1e-5f);
    ASSERT_NEAR(grid.cells[0].shape.v[1], 0.5f, 1e-5f);

    /* Vec10 blended */
    ASSERT_NEAR(grid.cells[0].external.v[0], 0.5f, 1e-5f);
    ASSERT_NEAR(grid.cells[0].external.v[1], 0.5f, 1e-5f);

    shape_smoother_free(&ss);
    free(grid.cells);
}

/* ═══════════════════════════════════════════════
 *  C. ContrastSmoother tests
 * ═══════════════════════════════════════════════ */

UTEST(contrast_smoother, first_frame_passthrough) {
    AdaptiveContrast ac = { .floor = 0.02f, .ceil = 0.31f,
                            .frame_avg = 0.5f, .frame_p10 = 0.1f,
                            .frame_p90 = 0.9f };
    ContrastSmoother cs;
    contrast_smoother_init(&cs);
    contrast_smoother_apply(&cs, &ac, 0.3f);

    ASSERT_NEAR(ac.frame_avg, 0.5f, 1e-6f);
    ASSERT_NEAR(ac.frame_p10, 0.1f, 1e-6f);
    ASSERT_NEAR(ac.frame_p90, 0.9f, 1e-6f);
}

UTEST(contrast_smoother, second_frame_blends) {
    ContrastSmoother cs;
    contrast_smoother_init(&cs);

    /* Frame 1 */
    AdaptiveContrast ac = { .floor = 0.02f, .ceil = 0.31f,
                            .frame_avg = 0.5f, .frame_p10 = 0.1f,
                            .frame_p90 = 0.9f };
    contrast_smoother_apply(&cs, &ac, 0.5f);

    /* Frame 2: very different stats */
    ac.frame_avg = 0.8f;
    ac.frame_p10 = 0.3f;
    ac.frame_p90 = 1.0f;
    contrast_smoother_apply(&cs, &ac, 0.5f);

    /* Blended: 0.5 * frame2 + 0.5 * frame1 */
    ASSERT_NEAR(ac.frame_avg, 0.65f, 1e-5f); /* 0.5*0.8 + 0.5*0.5 */
    ASSERT_NEAR(ac.frame_p10, 0.20f, 1e-5f); /* 0.5*0.3 + 0.5*0.1 */
    ASSERT_NEAR(ac.frame_p90, 0.95f, 1e-5f); /* 0.5*1.0 + 0.5*0.9 */
}

UTEST(contrast_smoother, convergence) {
    ContrastSmoother cs;
    contrast_smoother_init(&cs);

    for (int f = 0; f < 50; f++) {
        AdaptiveContrast ac = { .floor = 0.02f, .ceil = 0.31f,
                                .frame_avg = 0.42f, .frame_p10 = 0.15f,
                                .frame_p90 = 0.78f };
        contrast_smoother_apply(&cs, &ac, 0.3f);
    }

    ASSERT_NEAR(cs.smooth_avg, 0.42f, 1e-3f);
    ASSERT_NEAR(cs.smooth_p10, 0.15f, 1e-3f);
    ASSERT_NEAR(cs.smooth_p90, 0.78f, 1e-3f);
}

/* ═══════════════════════════════════════════════
 *  D. MatchSmoother tests
 * ═══════════════════════════════════════════════ */

UTEST(match_smoother, first_frame_normal_match) {
    CharDatabase db = make_test_db();
    Grid grid = make_grid(1, 1);
    /* Shape close to A (1,0,0,0,0,0) */
    grid.cells[0].shape = (Vec6){{0.8f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f}};

    MatchSmoother ms;
    match_smoother_init(&ms);
    match_smoother_apply(&ms, &grid, &db, 0.15f);

    ASSERT_EQ(grid.cells[0].ch, 'A');

    match_smoother_free(&ms);
    free(grid.cells);
}

UTEST(match_smoother, hysteresis_prevents_marginal_switch) {
    CharDatabase db = make_test_db();
    Grid grid = make_grid(1, 1);

    MatchSmoother ms;
    match_smoother_init(&ms);

    /* Frame 1: clearly matches A */
    grid.cells[0].shape = (Vec6){{0.8f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f}};
    match_smoother_apply(&ms, &grid, &db, 0.15f);
    ASSERT_EQ(grid.cells[0].ch, 'A');

    /* Frame 2: just barely closer to B than A.
     * shape (0.48, 0.52, 0, 0, 0, 0):
     *   dist_sq(A) = 0.52^2 + 0.52^2 = 0.5408
     *   dist_sq(B) = 0.48^2 + 0.48^2 = 0.4608
     *   threshold = 0.4608 < 0.5408 * 0.85 = 0.4597?  NO (0.4608 > 0.4597)
     *   → keeps A */
    grid.cells[0].shape = (Vec6){{0.48f, 0.52f, 0.0f, 0.0f, 0.0f, 0.0f}};
    match_smoother_apply(&ms, &grid, &db, 0.15f);
    ASSERT_EQ(grid.cells[0].ch, 'A');

    match_smoother_free(&ms);
    free(grid.cells);
}

UTEST(match_smoother, allows_significant_switch) {
    CharDatabase db = make_test_db();
    Grid grid = make_grid(1, 1);

    MatchSmoother ms;
    match_smoother_init(&ms);

    /* Frame 1: matches A */
    grid.cells[0].shape = (Vec6){{0.8f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f}};
    match_smoother_apply(&ms, &grid, &db, 0.15f);
    ASSERT_EQ(grid.cells[0].ch, 'A');

    /* Frame 2: clearly closer to B
     * shape (0.3, 0.7, 0, 0, 0, 0):
     *   dist_sq(A) = 0.49 + 0.49 = 0.98
     *   dist_sq(B) = 0.09 + 0.09 = 0.18
     *   0.18 < 0.98 * 0.85 = 0.833  → YES, switch to B */
    grid.cells[0].shape = (Vec6){{0.3f, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f}};
    match_smoother_apply(&ms, &grid, &db, 0.15f);
    ASSERT_EQ(grid.cells[0].ch, 'B');

    match_smoother_free(&ms);
    free(grid.cells);
}

UTEST(match_smoother, hysteresis0_same_as_normal) {
    CharDatabase db = make_test_db();
    Grid grid = make_grid(1, 1);

    MatchSmoother ms;
    match_smoother_init(&ms);

    /* Frame 1: matches A */
    grid.cells[0].shape = (Vec6){{0.8f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f}};
    match_smoother_apply(&ms, &grid, &db, 0.0f);
    ASSERT_EQ(grid.cells[0].ch, 'A');

    /* Frame 2: slightly closer to B — with hysteresis=0 should switch
     * shape (0.45, 0.55, 0, 0, 0, 0):
     *   dist_sq(A) = 0.3025 + 0.3025 = 0.605
     *   dist_sq(B) = 0.2025 + 0.2025 = 0.405
     *   threshold=1.0: 0.405 < 0.605 * 1.0  → switch to B */
    grid.cells[0].shape = (Vec6){{0.45f, 0.55f, 0.0f, 0.0f, 0.0f, 0.0f}};
    match_smoother_apply(&ms, &grid, &db, 0.0f);
    ASSERT_EQ(grid.cells[0].ch, 'B');

    match_smoother_free(&ms);
    free(grid.cells);
}

UTEST(match_smoother, grid_resize_resets) {
    CharDatabase db = make_test_db();
    Grid grid = make_grid(1, 1);
    grid.cells[0].shape = (Vec6){{0.8f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f}};

    MatchSmoother ms;
    match_smoother_init(&ms);
    match_smoother_apply(&ms, &grid, &db, 0.15f);
    ASSERT_EQ(grid.cells[0].ch, 'A');
    ASSERT_TRUE(ms.ready);
    free(grid.cells);

    /* Resize: new grid with 2 cells */
    grid = make_grid(1, 2);
    grid.cells[0].shape = (Vec6){{0.2f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f}};
    grid.cells[1].shape = (Vec6){{0.9f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f}};
    match_smoother_apply(&ms, &grid, &db, 0.15f);

    /* After reset, first frame does normal matching */
    ASSERT_EQ(grid.cells[0].ch, 'B');
    ASSERT_EQ(grid.cells[1].ch, 'A');

    match_smoother_free(&ms);
    free(grid.cells);
}

UTEST_MAIN();
