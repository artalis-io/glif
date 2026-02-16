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

UTEST(contrast, directional_uniform_zeroes) {
    /* If internal == external, directional contrast should produce zero */
    Vec6 shape = {{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}};
    Vec10 ext = {{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}};
    Grid grid = make_single_cell_grid(shape, ext);

    SamplingConfig sc;
    sampling_config_init(&sc);

    contrast_directional(&grid, &sc, 1.0f);

    /* All diffs are 0, so result should be zero vector */
    ASSERT_NEAR(vec6_length(grid.cells[0].shape), 0.0f, 1e-5f);

    free(grid.cells);
}

UTEST(contrast, directional_diff_nonzero) {
    /* If internal differs from external, we should get nonzero contrast */
    Vec6 shape = {{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
    Vec10 ext = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    Grid grid = make_single_cell_grid(shape, ext);

    SamplingConfig sc;
    sampling_config_init(&sc);

    contrast_directional(&grid, &sc, 1.0f);

    ASSERT_GT(vec6_length(grid.cells[0].shape), 0.1f);

    free(grid.cells);
}

UTEST(contrast, global_normalizes) {
    Vec6 shape = {{0.2f, 0.4f, 0.6f, 0.8f, 1.0f, 0.5f}};
    Vec10 ext = vec10_zero();
    Grid grid = make_single_cell_grid(shape, ext);

    contrast_global(&grid, 1.0f);

    /* After global contrast with crunch=1, result should be normalized */
    float len = vec6_length(grid.cells[0].shape);
    ASSERT_NEAR(len, 1.0f, 1e-4f);

    free(grid.cells);
}

UTEST(contrast, global_crunch_sharpens) {
    Vec6 shape = {{0.2f, 0.4f, 0.6f, 0.8f, 1.0f, 0.5f}};
    Vec10 ext = vec10_zero();
    Grid grid1 = make_single_cell_grid(shape, ext);
    Grid grid2 = make_single_cell_grid(shape, ext);

    contrast_global(&grid1, 1.0f);
    contrast_global(&grid2, 3.0f);

    /* Higher crunch should make small values smaller (sharper contrast) */
    /* The smallest component (0.2/1.0 = 0.2) should be more suppressed with crunch=3 */
    /* After crunch=1: component = 0.2, after crunch=3: component = 0.008 (before renorm) */
    /* Just check they produce different results */
    float diff = vec6_dist_sq(grid1.cells[0].shape, grid2.cells[0].shape);
    ASSERT_GT(diff, 0.01f);

    free(grid1.cells);
    free(grid2.cells);
}

UTEST(contrast, zero_length_vector_stays_zero) {
    /* A zero shape vector should remain zero after both contrast operations */
    Vec6 shape = vec6_zero();
    Vec10 ext = vec10_zero();
    Grid grid = make_single_cell_grid(shape, ext);

    SamplingConfig sc;
    sampling_config_init(&sc);

    contrast_directional(&grid, &sc, 1.25f);
    ASSERT_NEAR(vec6_length(grid.cells[0].shape), 0.0f, 1e-8f);

    contrast_global(&grid, 1.5f);
    ASSERT_NEAR(vec6_length(grid.cells[0].shape), 0.0f, 1e-8f);

    free(grid.cells);
}

UTEST(contrast, directional_crunch_zero_behavior) {
    /* With crunch=0, pow(x, 0) = 1.0 for all x > 0, so all components
     * should become equal (after normalization in the directional step,
     * each nonzero component raised to 0 power = 1) */
    Vec6 shape = {{1.0f, 0.5f, 0.8f, 0.3f, 0.9f, 0.7f}};
    Vec10 ext = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    Grid grid = make_single_cell_grid(shape, ext);

    SamplingConfig sc;
    sampling_config_init(&sc);

    /* With crunch=0, pow(x, 0) = 1 for x > 0 */
    contrast_directional(&grid, &sc, 0.0f);

    /* All 6 components should be 1.0 (since all diffs are nonzero -> normalized -> pow(x,0)=1) */
    for (int i = 0; i < 6; i++) {
        ASSERT_NEAR(grid.cells[0].shape.v[i], 1.0f, 1e-5f);
    }

    free(grid.cells);
}

UTEST(contrast, two_by_two_grid) {
    /* Test that contrast processing works on a 2x2 grid (multiple cells) */
    Grid grid = make_grid(2, 2);

    /* Set up each cell with different shape/external values */
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

    contrast_directional(&grid, &sc, 1.25f);

    /* Cell 0 and 3 should have nonzero contrast (different internal/external) */
    ASSERT_GT(vec6_length(grid.cells[0].shape), 0.1f);
    ASSERT_GT(vec6_length(grid.cells[3].shape), 0.1f);

    /* Cell 2 was uniform, so should produce zero contrast */
    ASSERT_NEAR(vec6_length(grid.cells[2].shape), 0.0f, 1e-5f);

    contrast_global(&grid, 1.5f);

    /* After global, all nonzero vectors should be normalized */
    for (int i = 0; i < 4; i++) {
        float len = vec6_length(grid.cells[i].shape);
        if (len > 1e-6f) {
            ASSERT_NEAR(len, 1.0f, 0.01f);
        }
    }

    free(grid.cells);
}

UTEST(contrast, global_zero_shape_stays_zero) {
    Vec6 shape = vec6_zero();
    Vec10 ext = vec10_zero();
    Grid grid = make_single_cell_grid(shape, ext);

    contrast_global(&grid, 2.0f);

    /* Zero shape should remain zero after global contrast */
    ASSERT_NEAR(vec6_length(grid.cells[0].shape), 0.0f, 1e-8f);

    free(grid.cells);
}

UTEST_MAIN();
