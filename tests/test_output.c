#include "utest.h"
#include "output.h"
#include "font.h"
#include "grid.h"
#include "image.h"
#include "sampling.h"
#include "vec6.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/* Helper: create a small grid with known characters and colors */
static Grid make_test_grid(int rows, int cols, int cell_w, int cell_h, char fill_ch) {
    Grid grid;
    grid.rows = rows;
    grid.cols = cols;
    grid.cell_w = cell_w;
    grid.cell_h = cell_h;
    grid.cells = calloc((size_t)(rows * cols), sizeof(GridCell));
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            GridCell *cell = &grid.cells[r * cols + c];
            cell->px = c * cell_w;
            cell->py = r * cell_h;
            cell->ch = fill_ch;
            cell->r = 200;
            cell->g = 100;
            cell->b = 50;
        }
    }
    return grid;
}

/*
 * Helper: redirect stdout to a file, call output_plain, restore stdout.
 * Returns 0 on success, -1 on failure.
 */
static int capture_plain_output(const Grid *grid, const char *path) {
    fflush(stdout);
    int saved_fd = dup(STDOUT_FILENO);
    if (saved_fd < 0) return -1;

    int file_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd < 0) { close(saved_fd); return -1; }

    dup2(file_fd, STDOUT_FILENO);
    close(file_fd);

    output_plain(grid);

    fflush(stdout);
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);
    return 0;
}

UTEST(output, ppm_writes_valid_header) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, "fonts/SFNSMono.ttf", 10, 20, &sc);
    if (ret != 0) return;

    Grid grid = make_test_grid(3, 4, 10, 20, 'A');

    const char *path = "/tmp/glif_test_header.ppm";
    ret = output_ppm(&grid, &db, path, 1, 0);
    ASSERT_EQ(ret, 0);

    /* Read back the file and verify PPM header */
    FILE *f = fopen(path, "rb");
    ASSERT_TRUE(f != NULL);

    char magic[3];
    int w, h, maxval;
    int scanned = fscanf(f, "%2s %d %d %d", magic, &w, &h, &maxval);
    ASSERT_EQ(scanned, 4);
    ASSERT_STREQ(magic, "P6");
    ASSERT_EQ(w, 4 * 10 * 1);  /* cols * cell_w * scale */
    ASSERT_EQ(h, 3 * 20 * 1);  /* rows * cell_h * scale */
    ASSERT_EQ(maxval, 255);

    fclose(f);
    remove(path);
    free(grid.cells);
    char_db_free(&db);
}

UTEST(output, ppm_scale1_vs_scale4_different_sizes) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, "fonts/SFNSMono.ttf", 10, 20, &sc);
    if (ret != 0) return;

    Grid grid = make_test_grid(2, 3, 10, 20, 'X');

    const char *path1 = "/tmp/glif_test_scale1.ppm";
    const char *path4 = "/tmp/glif_test_scale4.ppm";

    ret = output_ppm(&grid, &db, path1, 1, 0);
    ASSERT_EQ(ret, 0);
    ret = output_ppm(&grid, &db, path4, 4, 0);
    ASSERT_EQ(ret, 0);

    /* Compare file sizes */
    FILE *f1 = fopen(path1, "rb");
    FILE *f4 = fopen(path4, "rb");
    ASSERT_TRUE(f1 != NULL);
    ASSERT_TRUE(f4 != NULL);

    fseek(f1, 0, SEEK_END);
    long size1 = ftell(f1);
    fseek(f4, 0, SEEK_END);
    long size4 = ftell(f4);

    fclose(f1);
    fclose(f4);

    /* Scale=4 should produce a much larger file than scale=1 */
    ASSERT_GT(size4, size1);

    remove(path1);
    remove(path4);
    free(grid.cells);
    char_db_free(&db);
}

UTEST(output, ppm_file_size_matches_expected) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, "fonts/SFNSMono.ttf", 10, 20, &sc);
    if (ret != 0) return;

    int rows = 2, cols = 3, cell_w = 10, cell_h = 20, scale = 1;
    Grid grid = make_test_grid(rows, cols, cell_w, cell_h, 'H');

    const char *path = "/tmp/glif_test_size.ppm";
    ret = output_ppm(&grid, &db, path, scale, 0);
    ASSERT_EQ(ret, 0);

    /* Calculate expected pixel data size */
    int img_w = cols * cell_w * scale;
    int img_h = rows * cell_h * scale;
    long pixel_data_size = (long)img_w * (long)img_h * 3;

    /* Calculate header size: "P6\n{w} {h}\n255\n" */
    char header_buf[64];
    int header_len = snprintf(header_buf, sizeof(header_buf),
                               "P6\n%d %d\n255\n", img_w, img_h);

    long expected_size = header_len + pixel_data_size;

    FILE *f = fopen(path, "rb");
    ASSERT_TRUE(f != NULL);
    fseek(f, 0, SEEK_END);
    long actual_size = ftell(f);
    fclose(f);

    ASSERT_EQ(actual_size, expected_size);

    remove(path);
    free(grid.cells);
    char_db_free(&db);
}

UTEST(output, plain_correct_lines_and_columns) {
    Grid grid = make_test_grid(3, 5, 10, 20, 'Q');

    const char *path = "/tmp/glif_test_plain.txt";
    ASSERT_EQ(capture_plain_output(&grid, path), 0);

    /* Read back and verify */
    FILE *rf = fopen(path, "r");
    ASSERT_TRUE(rf != NULL);

    int line_count = 0;
    char line_buf[256];
    while (fgets(line_buf, sizeof(line_buf), rf)) {
        int len = (int)strlen(line_buf);
        if (len > 0 && line_buf[len - 1] == '\n') len--;
        ASSERT_EQ(len, 5);  /* cols = 5 */
        for (int i = 0; i < len; i++) {
            ASSERT_EQ(line_buf[i], 'Q');
        }
        line_count++;
    }
    ASSERT_EQ(line_count, 3);  /* rows = 3 */

    fclose(rf);
    remove(path);
    free(grid.cells);
}

UTEST(output, plain_roundtrip_known_chars) {
    Grid grid;
    grid.rows = 2;
    grid.cols = 4;
    grid.cell_w = 10;
    grid.cell_h = 20;
    grid.cells = calloc(8, sizeof(GridCell));
    ASSERT_TRUE(grid.cells != NULL);

    const char *expected_chars = "ABCD1234";
    for (int i = 0; i < 8; i++) {
        grid.cells[i].ch = expected_chars[i];
    }

    const char *path = "/tmp/glif_test_roundtrip.txt";
    ASSERT_EQ(capture_plain_output(&grid, path), 0);

    /* Read back */
    FILE *rf = fopen(path, "r");
    ASSERT_TRUE(rf != NULL);

    char line1[256], line2[256];
    ASSERT_TRUE(fgets(line1, sizeof(line1), rf) != NULL);
    ASSERT_TRUE(fgets(line2, sizeof(line2), rf) != NULL);

    ASSERT_EQ(line1[0], 'A');
    ASSERT_EQ(line1[1], 'B');
    ASSERT_EQ(line1[2], 'C');
    ASSERT_EQ(line1[3], 'D');

    ASSERT_EQ(line2[0], '1');
    ASSERT_EQ(line2[1], '2');
    ASSERT_EQ(line2[2], '3');
    ASSERT_EQ(line2[3], '4');

    fclose(rf);
    remove(path);
    free(grid.cells);
}

UTEST(output, ppm_with_scale4_file_size) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, "fonts/SFNSMono.ttf", 10, 20, &sc);
    if (ret != 0) return;

    int rows = 2, cols = 3, cell_w = 10, cell_h = 20, scale = 4;
    Grid grid = make_test_grid(rows, cols, cell_w, cell_h, 'W');

    const char *path = "/tmp/glif_test_size4.ppm";
    ret = output_ppm(&grid, &db, path, scale, 0);
    ASSERT_EQ(ret, 0);

    int img_w = cols * cell_w * scale;
    int img_h = rows * cell_h * scale;
    long pixel_data_size = (long)img_w * (long)img_h * 3;

    char header_buf[64];
    int header_len = snprintf(header_buf, sizeof(header_buf),
                               "P6\n%d %d\n255\n", img_w, img_h);
    long expected_size = header_len + pixel_data_size;

    FILE *f = fopen(path, "rb");
    ASSERT_TRUE(f != NULL);
    fseek(f, 0, SEEK_END);
    long actual_size = ftell(f);
    fclose(f);

    ASSERT_EQ(actual_size, expected_size);

    remove(path);
    free(grid.cells);
    char_db_free(&db);
}

UTEST(output, ppm_invalid_path_fails) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, "fonts/SFNSMono.ttf", 10, 20, &sc);
    if (ret != 0) return;

    Grid grid = make_test_grid(1, 1, 10, 20, 'X');

    /* Try writing to a nonexistent directory */
    ret = output_ppm(&grid, &db, "/nonexistent_dir/test.ppm", 1, 0);
    ASSERT_NE(ret, 0);

    free(grid.cells);
    char_db_free(&db);
}

UTEST_MAIN();
