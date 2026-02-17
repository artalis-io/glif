#include "utest.h"
#include "vec6.h"
#include "sampling.h"
#include "grid.h"
#include "contrast.h"
#include <stdlib.h>
#include <math.h>

/* Helper: create a minimal 1-cell grid with given shape and external vectors */
static Grid make_single_cell_grid(Vec6 shape, Vec10 ext) {
    Grid grid;
    grid.rows = 1;
    grid.cols = 1;
    grid.cell_w = 10;
    grid.cell_h = 20;
    grid.cells = calloc(1, sizeof(GridCell));
    grid.cells[0].shape = shape;
    grid.cells[0].external = ext;
    return grid;
}

/* Helper: create a multi-cell grid */
static Grid make_grid(int rows, int cols) {
    Grid grid;
    grid.rows = rows;
    grid.cols = cols;
    grid.cell_w = 10;
    grid.cell_h = 20;
    grid.cells = calloc((size_t)(rows * cols), sizeof(GridCell));
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            grid.cells[r * cols + c].px = c * 10;
            grid.cells[r * cols + c].py = r * 20;
        }
    }
    return grid;
}

UTEST(contrast, directional_uniform_preserves_values) {
    /* If internal == external, (val/max)^exp * max = val (since val == max).
     * Values should be preserved. */
    Vec6 shape = {{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}};
    Vec10 ext = {{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}};
    Grid grid = make_single_cell_grid(shape, ext);

    SamplingConfig sc;
    sampling_config_init(&sc);

    contrast_directional(&grid, &sc, 2.0f, NULL);

    /* val/max = 1.0, pow(1.0, 2.0) * 0.5 = 0.5 — values preserved */
    for (int i = 0; i < 6; i++) {
        ASSERT_NEAR(grid.cells[0].shape.v[i], 0.5f, 1e-5f);
    }

    free(grid.cells);
}

UTEST(contrast, directional_suppresses_when_external_brighter) {
    /* When external > internal, val/max < 1, so crunching suppresses */
    Vec6 shape = {{0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f}};
    Vec10 ext = {{0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f}};
    Grid grid = make_single_cell_grid(shape, ext);

    SamplingConfig sc;
    sampling_config_init(&sc);

    contrast_directional(&grid, &sc, 2.0f, NULL);

    /* (0.3/0.9)^2 * 0.9 = (1/3)^2 * 0.9 = 0.1 */
    for (int i = 0; i < 6; i++) {
        ASSERT_LT(grid.cells[0].shape.v[i], 0.3f);
        ASSERT_GT(grid.cells[0].shape.v[i], 0.0f);
    }

    free(grid.cells);
}

UTEST(contrast, global_crunch1_preserves_values) {
    /* With crunch=1: (val/max)^1 * max = val — identity */
    Vec6 shape = {{0.2f, 0.4f, 0.6f, 0.8f, 1.0f, 0.5f}};
    Vec10 ext = vec10_zero();
    Grid grid = make_single_cell_grid(shape, ext);

    contrast_global(&grid, 1.0f, NULL);

    ASSERT_NEAR(grid.cells[0].shape.v[0], 0.2f, 1e-5f);
    ASSERT_NEAR(grid.cells[0].shape.v[1], 0.4f, 1e-5f);
    ASSERT_NEAR(grid.cells[0].shape.v[2], 0.6f, 1e-5f);
    ASSERT_NEAR(grid.cells[0].shape.v[3], 0.8f, 1e-5f);
    ASSERT_NEAR(grid.cells[0].shape.v[4], 1.0f, 1e-5f);
    ASSERT_NEAR(grid.cells[0].shape.v[5], 0.5f, 1e-5f);

    free(grid.cells);
}

UTEST(contrast, global_crunch_sharpens) {
    Vec6 shape = {{0.2f, 0.4f, 0.6f, 0.8f, 1.0f, 0.5f}};
    Vec10 ext = vec10_zero();
    Grid grid = make_single_cell_grid(shape, ext);

    contrast_global(&grid, 3.0f, NULL);

    /* Max component (1.0) stays 1.0: (1.0/1.0)^3 * 1.0 = 1.0 */
    ASSERT_NEAR(grid.cells[0].shape.v[4], 1.0f, 1e-5f);

    /* Smaller components are suppressed: (0.2/1.0)^3 * 1.0 = 0.008 */
    ASSERT_NEAR(grid.cells[0].shape.v[0], 0.008f, 1e-4f);

    /* (0.5/1.0)^3 * 1.0 = 0.125 */
    ASSERT_NEAR(grid.cells[0].shape.v[5], 0.125f, 1e-4f);

    free(grid.cells);
}

UTEST(contrast, zero_length_vector_stays_zero) {
    Vec6 shape = vec6_zero();
    Vec10 ext = vec10_zero();
    Grid grid = make_single_cell_grid(shape, ext);

    SamplingConfig sc;
    sampling_config_init(&sc);

    contrast_directional(&grid, &sc, 1.25f, NULL);
    ASSERT_NEAR(vec6_length(grid.cells[0].shape), 0.0f, 1e-8f);

    contrast_global(&grid, 1.5f, NULL);
    ASSERT_NEAR(vec6_length(grid.cells[0].shape), 0.0f, 1e-8f);

    free(grid.cells);
}

UTEST(contrast, directional_preserves_scale) {
    /* Directional contrast preserves the max(internal, external) scale */
    Vec6 shape = {{1.0f, 0.5f, 0.8f, 0.3f, 0.9f, 0.7f}};
    Vec10 ext = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    Grid grid = make_single_cell_grid(shape, ext);

    SamplingConfig sc;
    sampling_config_init(&sc);

    contrast_directional(&grid, &sc, 1.0f, NULL);

    /* With crunch=1 and external=0, max_val=internal_val,
     * so (val/val)^1 * val = val — values preserved */
    ASSERT_NEAR(grid.cells[0].shape.v[0], 1.0f, 1e-5f);
    ASSERT_NEAR(grid.cells[0].shape.v[1], 0.5f, 1e-5f);

    free(grid.cells);
}

UTEST(contrast, two_by_two_grid) {
    Grid grid = make_grid(2, 2);

    grid.cells[0].shape = (Vec6){{0.8f, 0.2f, 0.5f, 0.3f, 0.7f, 0.1f}};
    grid.cells[0].external = (Vec10){{0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f}};

    grid.cells[1].shape = (Vec6){{0.1f, 0.9f, 0.4f, 0.6f, 0.2f, 0.8f}};
    grid.cells[1].external = (Vec10){{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}};

    grid.cells[2].shape = (Vec6){{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}};
    grid.cells[2].external = (Vec10){{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}};

    grid.cells[3].shape = (Vec6){{1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f}};
    grid.cells[3].external = (Vec10){{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};

    SamplingConfig sc;
    sampling_config_init(&sc);

    contrast_directional(&grid, &sc, 1.25f, NULL);

    /* Cell 0: internal > external, values should be modified but stay positive */
    ASSERT_GT(vec6_length(grid.cells[0].shape), 0.1f);

    /* Cell 2: uniform internal == external, values preserved */
    ASSERT_NEAR(grid.cells[2].shape.v[0], 0.5f, 1e-4f);

    /* Cell 3: alternating 1.0/0.0, with crunch=1.0 and ext=0, 1.0 stays, 0.0 stays */
    ASSERT_NEAR(grid.cells[3].shape.v[0], 1.0f, 0.01f);

    contrast_global(&grid, 1.5f, NULL);

    /* After global, max component should be preserved */
    float max3 = vec6_max_component(grid.cells[0].shape);
    ASSERT_GT(max3, 0.0f);

    free(grid.cells);
}

UTEST(contrast, global_zero_shape_stays_zero) {
    Vec6 shape = vec6_zero();
    Vec10 ext = vec10_zero();
    Grid grid = make_single_cell_grid(shape, ext);

    contrast_global(&grid, 2.0f, NULL);

    ASSERT_NEAR(vec6_length(grid.cells[0].shape), 0.0f, 1e-8f);

    free(grid.cells);
}

UTEST(contrast, analyze_frame_computes_stats) {
    Grid grid = make_grid(1, 5);
    /* 5 cells with increasing brightness */
    for (int i = 0; i < 5; i++) {
        uint8_t val = (uint8_t)(i * 50 + 25);  /* 25, 75, 125, 175, 225 */
        grid.cells[i].r = val;
        grid.cells[i].g = val;
        grid.cells[i].b = val;
    }

    AdaptiveContrast ac = { .floor = 0.02f, .ceil = 0.31f };
    contrast_analyze_frame(&ac, &grid);

    /* avg = mean of (25,75,125,175,225)/255 = 125/255 ≈ 0.49 */
    ASSERT_NEAR(ac.frame_avg, 125.0f / 255.0f, 0.02f);
    /* p10 and p90 should bracket the range */
    ASSERT_LT(ac.frame_p10, ac.frame_avg);
    ASSERT_GT(ac.frame_p90, ac.frame_avg);

    free(grid.cells);
}

UTEST(contrast, adaptive_dark_frame_less_crunch_than_bright) {
    /* Two identical grids, one "dark frame" and one "bright frame".
     * The dark frame should get less effective crunch for the same cell. */
    Vec6 shape = {{0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f}};
    Vec10 ext = {{0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f}};

    /* Dark frame: all cells dim */
    Grid dark_grid = make_grid(1, 4);
    for (int i = 0; i < 4; i++) {
        dark_grid.cells[i].shape = shape;
        dark_grid.cells[i].external = ext;
        dark_grid.cells[i].r = 20;
        dark_grid.cells[i].g = 20;
        dark_grid.cells[i].b = 20;
    }

    /* Bright frame: all cells bright */
    Grid bright_grid = make_grid(1, 4);
    for (int i = 0; i < 4; i++) {
        bright_grid.cells[i].shape = shape;
        bright_grid.cells[i].external = ext;
        bright_grid.cells[i].r = 200;
        bright_grid.cells[i].g = 200;
        bright_grid.cells[i].b = 200;
    }

    SamplingConfig sc;
    sampling_config_init(&sc);
    AdaptiveContrast ac_dark = { .floor = 0.02f, .ceil = 0.31f };
    AdaptiveContrast ac_bright = { .floor = 0.02f, .ceil = 0.31f };

    contrast_analyze_frame(&ac_dark, &dark_grid);
    contrast_analyze_frame(&ac_bright, &bright_grid);

    contrast_directional(&dark_grid, &sc, 2.5f, &ac_dark);
    contrast_directional(&bright_grid, &sc, 2.5f, &ac_bright);

    /* Dark frame shape values should be higher (less suppressed) than bright */
    float dark_len = vec6_length(dark_grid.cells[0].shape);
    float bright_len = vec6_length(bright_grid.cells[0].shape);
    ASSERT_GT(dark_len, bright_len);

    free(dark_grid.cells);
    free(bright_grid.cells);
}

UTEST_MAIN();
