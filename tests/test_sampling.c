#include "utest.h"
#include "sampling.h"
#include <stdlib.h>
#include <math.h>

UTEST(sampling, config_init_internal_count) {
    GlifSamplingConfig sc;
    glif_sampling_config_init(&sc);

    /* Verify 6 internal circles have reasonable positions */
    for (int i = 0; i < GLIF_NUM_INTERNAL; i++) {
        ASSERT_GE(sc.internal[i].cx, 0.0f);
        ASSERT_LE(sc.internal[i].cx, 1.0f);
        ASSERT_GE(sc.internal[i].cy, 0.0f);
        ASSERT_LE(sc.internal[i].cy, 1.0f);
        ASSERT_GT(sc.internal[i].r, 0.0f);
    }
}

UTEST(sampling, config_init_external_count) {
    GlifSamplingConfig sc;
    glif_sampling_config_init(&sc);

    /* External circles can extend outside [0,1] — just check they exist */
    for (int i = 0; i < GLIF_NUM_EXTERNAL; i++) {
        ASSERT_GT(sc.external[i].r, 0.0f);
    }
}

UTEST(sampling, config_affecting_table) {
    GlifSamplingConfig sc;
    glif_sampling_config_init(&sc);

    /* Each internal circle should have at least 2 affecting externals */
    for (int i = 0; i < GLIF_NUM_INTERNAL; i++) {
        ASSERT_GE(sc.affecting_count[i], 2);
        ASSERT_LE(sc.affecting_count[i], GLIF_MAX_AFFECTING);
        for (int j = 0; j < sc.affecting_count[i]; j++) {
            ASSERT_GE(sc.affecting[i][j], 0);
            ASSERT_LT(sc.affecting[i][j], GLIF_NUM_EXTERNAL);
        }
    }
}

UTEST(sampling, internal_circle_positions_match_plan) {
    /* Verify internal circles match the documented staggered 3x2 layout */
    GlifSamplingConfig sc;
    glif_sampling_config_init(&sc);

    /* Internal circle 0: top-left at (0.25, 0.20) */
    ASSERT_NEAR(sc.internal[0].cx, 0.25f, 1e-6f);
    ASSERT_NEAR(sc.internal[0].cy, 0.20f, 1e-6f);

    /* Internal circle 1: top-right at (0.75, 0.13) */
    ASSERT_NEAR(sc.internal[1].cx, 0.75f, 1e-6f);
    ASSERT_NEAR(sc.internal[1].cy, 0.13f, 1e-6f);

    /* Internal circle 2: mid-left at (0.25, 0.53) */
    ASSERT_NEAR(sc.internal[2].cx, 0.25f, 1e-6f);
    ASSERT_NEAR(sc.internal[2].cy, 0.53f, 1e-6f);

    /* Internal circle 3: mid-right at (0.75, 0.47) */
    ASSERT_NEAR(sc.internal[3].cx, 0.75f, 1e-6f);
    ASSERT_NEAR(sc.internal[3].cy, 0.47f, 1e-6f);

    /* Internal circle 4: bottom-left at (0.25, 0.87) */
    ASSERT_NEAR(sc.internal[4].cx, 0.25f, 1e-6f);
    ASSERT_NEAR(sc.internal[4].cy, 0.87f, 1e-6f);

    /* Internal circle 5: bottom-right at (0.75, 0.80) */
    ASSERT_NEAR(sc.internal[5].cx, 0.75f, 1e-6f);
    ASSERT_NEAR(sc.internal[5].cy, 0.80f, 1e-6f);

    /* All internal circles have radius 0.28 */
    for (int i = 0; i < GLIF_NUM_INTERNAL; i++) {
        ASSERT_NEAR(sc.internal[i].r, 0.28f, 1e-6f);
    }
}

UTEST(sampling, circle_average_uniform) {
    /* Create a uniform lightness map (all 0.5) */
    GlifLightnessMap lm;
    lm.width = 100;
    lm.height = 100;
    lm.data = malloc(100 * 100 * sizeof(float));
    ASSERT_TRUE(lm.data != NULL);
    for (int i = 0; i < 100 * 100; i++)
        lm.data[i] = 0.5f;

    float avg = glif_sampling_circle_average(&lm, 50.0f, 50.0f, 10.0f);
    ASSERT_NEAR(avg, 0.5f, 1e-5f);

    free(lm.data);
}

UTEST(sampling, circle_average_black_white) {
    /* Top half black (0), bottom half white (1) */
    GlifLightnessMap lm;
    lm.width = 100;
    lm.height = 100;
    lm.data = malloc(100 * 100 * sizeof(float));
    ASSERT_TRUE(lm.data != NULL);
    for (int y = 0; y < 100; y++)
        for (int x = 0; x < 100; x++)
            lm.data[y * 100 + x] = (y < 50) ? 0.0f : 1.0f;

    /* Circle in top half should be near 0 */
    float top = glif_sampling_circle_average(&lm, 50.0f, 25.0f, 10.0f);
    ASSERT_NEAR(top, 0.0f, 1e-5f);

    /* Circle in bottom half should be near 1 */
    float bot = glif_sampling_circle_average(&lm, 50.0f, 75.0f, 10.0f);
    ASSERT_NEAR(bot, 1.0f, 1e-5f);

    free(lm.data);
}

UTEST(sampling, circle_average_edge_clamp) {
    /* Circle at corner — should not crash, just sample what's available */
    GlifLightnessMap lm;
    lm.width = 20;
    lm.height = 20;
    lm.data = malloc(20 * 20 * sizeof(float));
    ASSERT_TRUE(lm.data != NULL);
    for (int i = 0; i < 20 * 20; i++)
        lm.data[i] = 1.0f;

    float avg = glif_sampling_circle_average(&lm, 0.0f, 0.0f, 5.0f);
    ASSERT_NEAR(avg, 1.0f, 1e-5f);

    free(lm.data);
}

UTEST(sampling, circle_average_radius_zero) {
    /* Circle with radius=0 is degenerate: only the center pixel (if any) */
    GlifLightnessMap lm;
    lm.width = 10;
    lm.height = 10;
    lm.data = malloc(10 * 10 * sizeof(float));
    ASSERT_TRUE(lm.data != NULL);
    for (int i = 0; i < 10 * 10; i++)
        lm.data[i] = 0.0f;
    /* Set center pixel to known value */
    lm.data[5 * 10 + 5] = 0.75f;

    float avg = glif_sampling_circle_average(&lm, 5.0f, 5.0f, 0.0f);
    /* radius=0 means r2=0, so only pixel at exact center passes dx*dx+dy*dy<=0 */
    ASSERT_NEAR(avg, 0.75f, 1e-5f);

    free(lm.data);
}

UTEST(sampling, circle_average_entirely_outside_right) {
    /* Circle entirely outside the image to the right */
    GlifLightnessMap lm;
    lm.width = 10;
    lm.height = 10;
    lm.data = malloc(10 * 10 * sizeof(float));
    ASSERT_TRUE(lm.data != NULL);
    for (int i = 0; i < 10 * 10; i++)
        lm.data[i] = 1.0f;

    /* Center at (100, 100) with radius 5 -- way outside 10x10 image */
    float avg = glif_sampling_circle_average(&lm, 100.0f, 100.0f, 5.0f);
    /* When completely outside, count=0, returns 0 */
    ASSERT_NEAR(avg, 0.0f, 1e-5f);

    free(lm.data);
}

UTEST(sampling, circle_average_entirely_outside_negative) {
    /* Circle entirely in negative coordinate space */
    GlifLightnessMap lm;
    lm.width = 10;
    lm.height = 10;
    lm.data = malloc(10 * 10 * sizeof(float));
    ASSERT_TRUE(lm.data != NULL);
    for (int i = 0; i < 10 * 10; i++)
        lm.data[i] = 1.0f;

    float avg = glif_sampling_circle_average(&lm, -50.0f, -50.0f, 5.0f);
    ASSERT_NEAR(avg, 0.0f, 1e-5f);

    free(lm.data);
}

UTEST(sampling, circle_average_single_pixel) {
    /* GlifImage is 1x1, circle covers it */
    GlifLightnessMap lm;
    lm.width = 1;
    lm.height = 1;
    lm.data = malloc(1 * sizeof(float));
    ASSERT_TRUE(lm.data != NULL);
    lm.data[0] = 0.42f;

    float avg = glif_sampling_circle_average(&lm, 0.0f, 0.0f, 1.0f);
    ASSERT_NEAR(avg, 0.42f, 1e-5f);

    free(lm.data);
}

UTEST_MAIN();
