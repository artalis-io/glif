#include "utest.h"
#include "image.h"
#include "sampling.h"
#include "grid.h"
#include <stdlib.h>
#include <string.h>

UTEST(grid, create_dimensions) {
    Image img;
    ASSERT_EQ(image_load(&img, "images/gradient.png"), 0);

    Grid grid;
    ASSERT_EQ(grid_create(&grid, &img, 10, 20), 0);
    ASSERT_EQ(grid.cols, 32);   /* 320 / 10 */
    ASSERT_EQ(grid.rows, 12);   /* 240 / 20 */
    ASSERT_EQ(grid.cell_w, 10);
    ASSERT_EQ(grid.cell_h, 20);
    ASSERT_TRUE(grid.cells != NULL);

    grid_free(&grid);
    image_free(&img);
}

UTEST(grid, cell_positions) {
    Image img;
    ASSERT_EQ(image_load(&img, "images/gradient.png"), 0);

    Grid grid;
    ASSERT_EQ(grid_create(&grid, &img, 10, 20), 0);

    /* First cell should be at (0, 0) */
    ASSERT_EQ(grid.cells[0].px, 0);
    ASSERT_EQ(grid.cells[0].py, 0);

    /* Second cell should be at (10, 0) */
    ASSERT_EQ(grid.cells[1].px, 10);
    ASSERT_EQ(grid.cells[1].py, 0);

    /* First cell of second row */
    ASSERT_EQ(grid.cells[grid.cols].px, 0);
    ASSERT_EQ(grid.cells[grid.cols].py, 20);

    grid_free(&grid);
    image_free(&img);
}

UTEST(grid, too_small_image) {
    Image img;
    img.width = 5;
    img.height = 5;
    img.channels = 3;
    img.pixels = NULL;

    Grid grid;
    /* Cell size larger than image should fail */
    ASSERT_NE(grid_create(&grid, &img, 10, 20), 0);
}

UTEST(grid, compute_vectors_runs) {
    Image img;
    ASSERT_EQ(image_load(&img, "images/checkerboard.png"), 0);

    LightnessMap lm;
    ASSERT_EQ(lightness_map_create(&lm, &img), 0);

    SamplingConfig sc;
    sampling_config_init(&sc);

    Grid grid;
    ASSERT_EQ(grid_create(&grid, &img, 10, 20), 0);
    grid_compute_vectors(&grid, &lm, &sc);

    /* After computing vectors, shapes should not all be zero */
    int any_nonzero = 0;
    for (int i = 0; i < grid.rows * grid.cols; i++) {
        if (vec6_length(grid.cells[i].shape) > 1e-6f) {
            any_nonzero = 1;
            break;
        }
    }
    ASSERT_TRUE(any_nonzero);

    grid_free(&grid);
    lightness_map_free(&lm);
    image_free(&img);
}

UTEST(grid, compute_colors) {
    Image img;
    ASSERT_EQ(image_load(&img, "images/gradient.png"), 0);

    Grid grid;
    ASSERT_EQ(grid_create(&grid, &img, 10, 20), 0);
    grid_compute_colors(&grid, &img);

    /* Top-left cell of gradient should have low R (x starts at 0) */
    ASSERT_LT(grid.cells[0].r, 20);
    /* Last column should have high R */
    ASSERT_GT(grid.cells[grid.cols - 1].r, 200);

    grid_free(&grid);
    image_free(&img);
}

UTEST(grid, odd_sized_image) {
    /* Image dimensions not evenly divisible by cell size: remainder is truncated */
    Image img;
    img.width = 35;  /* 35 / 10 = 3, remainder 5 */
    img.height = 55; /* 55 / 20 = 2, remainder 15 */
    img.channels = 3;
    img.pixels = calloc((size_t)(35 * 55 * 3), 1);
    ASSERT_TRUE(img.pixels != NULL);

    Grid grid;
    ASSERT_EQ(grid_create(&grid, &img, 10, 20), 0);
    ASSERT_EQ(grid.cols, 3);  /* 35 / 10 = 3 */
    ASSERT_EQ(grid.rows, 2);  /* 55 / 20 = 2 */

    /* Last cell pixel position should not exceed image bounds */
    GridCell *last = &grid.cells[grid.rows * grid.cols - 1];
    ASSERT_TRUE(last->px + grid.cell_w <= img.width);
    ASSERT_TRUE(last->py + grid.cell_h <= img.height);

    grid_free(&grid);
    free(img.pixels);
}

UTEST(grid, single_color_produces_uniform_color) {
    /* A solid red image should produce red color in all cells */
    Image img;
    img.width = 20;
    img.height = 40;
    img.channels = 3;
    img.pixels = malloc((size_t)(20 * 40 * 3));
    ASSERT_TRUE(img.pixels != NULL);
    for (int i = 0; i < 20 * 40; i++) {
        img.pixels[i * 3 + 0] = 200;  /* R */
        img.pixels[i * 3 + 1] = 50;   /* G */
        img.pixels[i * 3 + 2] = 100;  /* B */
    }

    Grid grid;
    ASSERT_EQ(grid_create(&grid, &img, 10, 20), 0);
    ASSERT_EQ(grid.rows, 2);
    ASSERT_EQ(grid.cols, 2);

    grid_compute_colors(&grid, &img);

    for (int i = 0; i < grid.rows * grid.cols; i++) {
        ASSERT_EQ(grid.cells[i].r, 200);
        ASSERT_EQ(grid.cells[i].g, 50);
        ASSERT_EQ(grid.cells[i].b, 100);
    }

    grid_free(&grid);
    free(img.pixels);
}

UTEST(grid, cells_dont_overlap) {
    Image img;
    ASSERT_EQ(image_load(&img, "images/gradient.png"), 0);

    Grid grid;
    ASSERT_EQ(grid_create(&grid, &img, 10, 20), 0);

    /* Check that no two cells share the same top-left pixel position */
    for (int i = 0; i < grid.rows * grid.cols; i++) {
        for (int j = i + 1; j < grid.rows * grid.cols; j++) {
            int same = (grid.cells[i].px == grid.cells[j].px &&
                        grid.cells[i].py == grid.cells[j].py);
            ASSERT_FALSE(same);
        }
    }

    /* Check that cells are non-overlapping: each cell's region is disjoint */
    for (int i = 0; i < grid.rows * grid.cols; i++) {
        for (int j = i + 1; j < grid.rows * grid.cols; j++) {
            int ix0 = grid.cells[i].px, iy0 = grid.cells[i].py;
            int ix1 = ix0 + grid.cell_w, iy1 = iy0 + grid.cell_h;
            int jx0 = grid.cells[j].px, jy0 = grid.cells[j].py;
            int jx1 = jx0 + grid.cell_w, jy1 = jy0 + grid.cell_h;

            /* Two rectangles overlap if they overlap on both axes */
            int overlap_x = (ix0 < jx1) && (jx0 < ix1);
            int overlap_y = (iy0 < jy1) && (jy0 < iy1);
            ASSERT_FALSE(overlap_x && overlap_y);
        }
    }

    grid_free(&grid);
    image_free(&img);
}

UTEST(grid, default_char_is_space) {
    Image img;
    img.width = 20;
    img.height = 20;
    img.channels = 3;
    img.pixels = calloc((size_t)(20 * 20 * 3), 1);
    ASSERT_TRUE(img.pixels != NULL);

    Grid grid;
    ASSERT_EQ(grid_create(&grid, &img, 10, 20), 0);

    /* All cells should have default char = space */
    for (int i = 0; i < grid.rows * grid.cols; i++) {
        ASSERT_EQ(grid.cells[i].ch, ' ');
    }

    grid_free(&grid);
    free(img.pixels);
}

UTEST(grid, free_nullifies_cells) {
    Image img;
    img.width = 20;
    img.height = 20;
    img.channels = 3;
    img.pixels = calloc((size_t)(20 * 20 * 3), 1);
    ASSERT_TRUE(img.pixels != NULL);

    Grid grid;
    ASSERT_EQ(grid_create(&grid, &img, 10, 20), 0);
    grid_free(&grid);
    ASSERT_TRUE(grid.cells == NULL);

    free(img.pixels);
}

UTEST_MAIN();
