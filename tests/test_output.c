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

/* ---- FrameDiff tests ---- */

UTEST(output, frame_diff_init_and_free) {
    FrameDiff fd;
    int ret = frame_diff_init(&fd, 10, 20);
    ASSERT_EQ(ret, 0);
    ASSERT_TRUE(fd.buf != NULL);
    ASSERT_TRUE(fd.prev != NULL);
    ASSERT_EQ(fd.cells, 200);
    ASSERT_EQ(fd.cols, 20);
    frame_diff_free(&fd);
    ASSERT_TRUE(fd.buf == NULL);
    ASSERT_TRUE(fd.prev == NULL);
}

UTEST(output, frame_diff_first_frame_renders_all_cells) {
    /* First frame: prev is zeroed, all cells differ → output should contain
     * every cell's character and color. */
    Grid grid = make_test_grid(3, 4, 10, 20, 'A');
    FrameDiff fd;
    ASSERT_EQ(frame_diff_init(&fd, 3, 4), 0);

    const char *path = "/tmp/glif_test_diff_first.txt";
    fflush(stdout);
    int saved_fd = dup(STDOUT_FILENO);
    int file_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(file_fd, STDOUT_FILENO);
    close(file_fd);

    frame_diff_render(&fd, &grid, 1);

    fflush(stdout);
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);

    FILE *f = fopen(path, "rb");
    ASSERT_TRUE(f != NULL);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);

    /* Should have substantial output (12 cells × ~22 bytes each) */
    ASSERT_GT(size, 200);

    /* After first frame, prev should be populated → identical frame = no output */
    const char *path2 = "/tmp/glif_test_diff_first2.txt";
    fflush(stdout);
    saved_fd = dup(STDOUT_FILENO);
    file_fd = open(path2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(file_fd, STDOUT_FILENO);
    close(file_fd);
    frame_diff_render(&fd, &grid, 1);
    fflush(stdout);
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);

    f = fopen(path2, "rb");
    ASSERT_TRUE(f != NULL);
    fseek(f, 0, SEEK_END);
    long size2 = ftell(f);
    fclose(f);
    ASSERT_EQ(size2, 0);  /* no changes → no output */

    remove(path);
    remove(path2);
    frame_diff_free(&fd);
    free(grid.cells);
}

UTEST(output, frame_diff_identical_frame_no_output) {
    /* Render once, then render the exact same grid → should produce no output */
    Grid grid = make_test_grid(2, 3, 10, 20, 'B');
    FrameDiff fd;
    ASSERT_EQ(frame_diff_init(&fd, 2, 3), 0);

    /* First render — populates prev */
    fflush(stdout);
    int saved_fd = dup(STDOUT_FILENO);
    int file_fd = open("/dev/null", O_WRONLY);
    dup2(file_fd, STDOUT_FILENO);
    close(file_fd);
    frame_diff_render(&fd, &grid, 1);
    fflush(stdout);
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);

    /* Second render — capture output */
    const char *path = "/tmp/glif_test_diff_nochange.txt";
    fflush(stdout);
    saved_fd = dup(STDOUT_FILENO);
    file_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(file_fd, STDOUT_FILENO);
    close(file_fd);
    frame_diff_render(&fd, &grid, 0);
    fflush(stdout);
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);

    /* File should be empty — nothing changed */
    FILE *f = fopen(path, "rb");
    ASSERT_TRUE(f != NULL);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    ASSERT_EQ(size, 0);

    remove(path);
    frame_diff_free(&fd);
    free(grid.cells);
}

UTEST(output, frame_diff_single_cell_change_uses_cursor_move) {
    /* Render once, change one cell, render again → output should contain \033[r;cH */
    Grid grid = make_test_grid(3, 4, 10, 20, 'C');
    FrameDiff fd;
    ASSERT_EQ(frame_diff_init(&fd, 3, 4), 0);

    /* First render */
    fflush(stdout);
    int saved_fd = dup(STDOUT_FILENO);
    int file_fd = open("/dev/null", O_WRONLY);
    dup2(file_fd, STDOUT_FILENO);
    close(file_fd);
    frame_diff_render(&fd, &grid, 1);
    fflush(stdout);
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);

    /* Change one cell at row 1, col 2 */
    grid.cells[1 * 4 + 2].ch = 'Z';
    grid.cells[1 * 4 + 2].r = 255;

    /* Second render — capture */
    const char *path = "/tmp/glif_test_diff_single.txt";
    fflush(stdout);
    saved_fd = dup(STDOUT_FILENO);
    file_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(file_fd, STDOUT_FILENO);
    close(file_fd);
    frame_diff_render(&fd, &grid, 1);
    fflush(stdout);
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);

    /* Should have output, and it should contain cursor positioning \033[2;3H */
    FILE *f = fopen(path, "rb");
    ASSERT_TRUE(f != NULL);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    ASSERT_GT(size, 0);

    /* Read content and look for the cursor position sequence */
    f = fopen(path, "rb");
    char *content = malloc((size_t)size);
    fread(content, 1, (size_t)size, f);
    fclose(f);

    /* Should contain \033[2;3H (row 2, col 3, 1-indexed) */
    int found = 0;
    for (long i = 0; i < size - 5; i++) {
        if (content[i] == '\033' && content[i+1] == '[' &&
            content[i+2] == '2' && content[i+3] == ';' &&
            content[i+4] == '3' && content[i+5] == 'H') {
            found = 1;
            break;
        }
    }
    ASSERT_TRUE(found);

    free(content);
    remove(path);
    frame_diff_free(&fd);
    free(grid.cells);
}

/* ---- PpmPipe tests ---- */

UTEST(output, ppm_pipe_init_and_free) {
    SamplingConfig sc;
    sampling_config_init(&sc);
    CharDatabase db;
    if (char_db_create(&db, "fonts/SFNSMono.ttf", 10, 20, &sc) != 0) return;

    Grid grid = make_test_grid(3, 4, 10, 20, 'A');

    PpmPipe pp;
    int ret = ppm_pipe_init(&pp, &grid, &db, 2, 0);
    ASSERT_EQ(ret, 0);
    ASSERT_TRUE(pp.pixels != NULL);
    ASSERT_TRUE(pp.render_bmps != NULL);
    ASSERT_EQ((int)pp.img_w, 4 * 10 * 2);
    ASSERT_EQ((int)pp.img_h, 3 * 20 * 2);
    ASSERT_EQ(pp.rw, 20);
    ASSERT_EQ(pp.rh, 40);

    ppm_pipe_free(&pp);
    ASSERT_TRUE(pp.pixels == NULL);
    ASSERT_TRUE(pp.render_bmps == NULL);

    free(grid.cells);
    char_db_free(&db);
}

UTEST(output, ppm_pipe_scale1_no_render_bmps) {
    SamplingConfig sc;
    sampling_config_init(&sc);
    CharDatabase db;
    if (char_db_create(&db, "fonts/SFNSMono.ttf", 10, 20, &sc) != 0) return;

    Grid grid = make_test_grid(2, 2, 10, 20, 'X');

    PpmPipe pp;
    int ret = ppm_pipe_init(&pp, &grid, &db, 1, 0);
    ASSERT_EQ(ret, 0);
    ASSERT_TRUE(pp.pixels != NULL);
    ASSERT_TRUE(pp.render_bmps == NULL);  /* scale=1 uses analysis bitmaps */
    ASSERT_EQ(pp.scale, 1);

    ppm_pipe_free(&pp);
    free(grid.cells);
    char_db_free(&db);
}

UTEST(output, ppm_pipe_frame_writes_valid_ppm) {
    SamplingConfig sc;
    sampling_config_init(&sc);
    CharDatabase db;
    if (char_db_create(&db, "fonts/SFNSMono.ttf", 10, 20, &sc) != 0) return;

    Grid grid = make_test_grid(2, 3, 10, 20, 'H');

    PpmPipe pp;
    ASSERT_EQ(ppm_pipe_init(&pp, &grid, &db, 1, 0), 0);

    /* Capture stdout */
    const char *path = "/tmp/glif_test_pipe_frame.ppm";
    fflush(stdout);
    int saved_fd = dup(STDOUT_FILENO);
    int file_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(file_fd, STDOUT_FILENO);
    close(file_fd);

    ppm_pipe_frame(&pp, &grid, &db);

    fflush(stdout);
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);

    /* Verify PPM header */
    FILE *f = fopen(path, "rb");
    ASSERT_TRUE(f != NULL);
    char magic[3];
    int w, h, maxval;
    int scanned = fscanf(f, "%2s %d %d %d", magic, &w, &h, &maxval);
    ASSERT_EQ(scanned, 4);
    ASSERT_STREQ(magic, "P6");
    ASSERT_EQ(w, (int)pp.img_w);
    ASSERT_EQ(h, (int)pp.img_h);
    ASSERT_EQ(maxval, 255);

    /* Verify total file size */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fclose(f);

    char header_buf[64];
    int header_len = snprintf(header_buf, sizeof(header_buf),
                              "P6\n%d %d\n255\n", w, h);
    long expected = header_len + (long)w * (long)h * 3;
    ASSERT_EQ(file_size, expected);

    remove(path);
    ppm_pipe_free(&pp);
    free(grid.cells);
    char_db_free(&db);
}

UTEST(output, ppm_pipe_dark_vs_light_differ) {
    SamplingConfig sc;
    sampling_config_init(&sc);
    CharDatabase db;
    if (char_db_create(&db, "fonts/SFNSMono.ttf", 10, 20, &sc) != 0) return;

    Grid grid = make_test_grid(2, 2, 10, 20, 'M');

    /* Render dark mode */
    PpmPipe pp_dark;
    ASSERT_EQ(ppm_pipe_init(&pp_dark, &grid, &db, 1, 1), 0);

    const char *path_dark = "/tmp/glif_test_pipe_dark.ppm";
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    int fd = open(path_dark, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(fd, STDOUT_FILENO); close(fd);
    ppm_pipe_frame(&pp_dark, &grid, &db);
    fflush(stdout);
    dup2(saved, STDOUT_FILENO); close(saved);

    /* Render light mode */
    PpmPipe pp_light;
    ASSERT_EQ(ppm_pipe_init(&pp_light, &grid, &db, 1, 0), 0);

    const char *path_light = "/tmp/glif_test_pipe_light.ppm";
    fflush(stdout);
    saved = dup(STDOUT_FILENO);
    fd = open(path_light, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(fd, STDOUT_FILENO); close(fd);
    ppm_pipe_frame(&pp_light, &grid, &db);
    fflush(stdout);
    dup2(saved, STDOUT_FILENO); close(saved);

    /* Both should be same size but different content */
    FILE *f1 = fopen(path_dark, "rb");
    FILE *f2 = fopen(path_light, "rb");
    ASSERT_TRUE(f1 && f2);

    fseek(f1, 0, SEEK_END); long s1 = ftell(f1);
    fseek(f2, 0, SEEK_END); long s2 = ftell(f2);
    ASSERT_EQ(s1, s2);

    /* Read and compare — should differ */
    rewind(f1); rewind(f2);
    uint8_t *d1 = malloc((size_t)s1);
    uint8_t *d2 = malloc((size_t)s2);
    fread(d1, 1, (size_t)s1, f1);
    fread(d2, 1, (size_t)s2, f2);
    fclose(f1); fclose(f2);

    int differ = 0;
    for (long i = 0; i < s1; i++) {
        if (d1[i] != d2[i]) { differ = 1; break; }
    }
    ASSERT_TRUE(differ);

    free(d1); free(d2);
    remove(path_dark);
    remove(path_light);
    ppm_pipe_free(&pp_dark);
    ppm_pipe_free(&pp_light);
    free(grid.cells);
    char_db_free(&db);
}

/* ---- raw_pipe_frame tests ---- */

UTEST(output, raw_pipe_frame_no_ppm_header) {
    SamplingConfig sc;
    sampling_config_init(&sc);
    CharDatabase db;
    if (char_db_create(&db, "fonts/SFNSMono.ttf", 10, 20, &sc) != 0) return;

    Grid grid = make_test_grid(2, 3, 10, 20, 'R');

    PpmPipe pp;
    ASSERT_EQ(ppm_pipe_init(&pp, &grid, &db, 1, 1), 0);

    /* Capture raw_pipe_frame output */
    const char *path = "/tmp/glif_test_raw_frame.bin";
    fflush(stdout);
    int saved_fd = dup(STDOUT_FILENO);
    int file_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(file_fd, STDOUT_FILENO);
    close(file_fd);

    raw_pipe_frame(&pp, &grid, &db);

    fflush(stdout);
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);

    /* File should be exactly img_w * img_h * 3 bytes (no PPM header) */
    FILE *f = fopen(path, "rb");
    ASSERT_TRUE(f != NULL);
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fclose(f);

    long expected = (long)(pp.img_w * pp.img_h * 3);
    ASSERT_EQ(file_size, expected);

    /* Verify it does NOT start with "P6" */
    f = fopen(path, "rb");
    char header[2] = {0};
    fread(header, 1, 2, f);
    fclose(f);
    ASSERT_TRUE(header[0] != 'P' || header[1] != '6');

    remove(path);
    ppm_pipe_free(&pp);
    free(grid.cells);
    char_db_free(&db);
}

UTEST(output, ppm_pipe_render_fills_buffer) {
    SamplingConfig sc;
    sampling_config_init(&sc);
    CharDatabase db;
    if (char_db_create(&db, "fonts/SFNSMono.ttf", 10, 20, &sc) != 0) return;

    Grid grid = make_test_grid(2, 2, 10, 20, 'M');

    PpmPipe pp;
    ASSERT_EQ(ppm_pipe_init(&pp, &grid, &db, 1, 1), 0);

    /* Zero out buffer, then call render */
    memset(pp.pixels, 0, pp.img_w * pp.img_h * 3);
    ppm_pipe_render(&pp, &grid, &db);

    /* With dark mode and colored cells (r=200,g=100,b=50), some pixels
     * should be non-zero where glyphs are rendered */
    int nonzero = 0;
    size_t total = pp.img_w * pp.img_h * 3;
    for (size_t i = 0; i < total; i++) {
        if (pp.pixels[i] != 0) { nonzero = 1; break; }
    }
    ASSERT_TRUE(nonzero);

    ppm_pipe_free(&pp);
    free(grid.cells);
    char_db_free(&db);
}

/* ---- GlifWriter tests ---- */

/* Helper: read little-endian values from buffer */
static uint16_t read_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static float read_f32(const uint8_t *p) {
    uint32_t bits = read_u32(p);
    float v;
    memcpy(&v, &bits, 4);
    return v;
}

UTEST(output, glif_writer_header) {
    const char *path = "/tmp/glif_test_header.glif";
    GlifWriter gw;
    int ret = glif_writer_init(&gw, path, 120, 40, 10, 20, 30.0f, 0);
    ASSERT_EQ(ret, 0);
    ret = glif_writer_finish(&gw);
    ASSERT_EQ(ret, 0);

    FILE *f = fopen(path, "rb");
    ASSERT_TRUE(f != NULL);
    uint8_t hdr[GLIF_HEADER_SIZE];
    ASSERT_EQ(fread(hdr, 1, GLIF_HEADER_SIZE, f), (size_t)GLIF_HEADER_SIZE);
    fclose(f);

    ASSERT_EQ(memcmp(hdr, "GLIF", 4), 0);
    ASSERT_EQ(hdr[4], GLIF_VERSION);
    ASSERT_EQ(hdr[5], 0);  /* flags: no dark mode */
    ASSERT_EQ(read_u16(hdr + 6), 120);
    ASSERT_EQ(read_u16(hdr + 8), 40);
    ASSERT_EQ(read_u16(hdr + 10), 10);  /* cell_w */
    ASSERT_EQ(read_u16(hdr + 12), 20);  /* cell_h */
    ASSERT_TRUE(read_f32(hdr + 14) > 29.9f && read_f32(hdr + 14) < 30.1f);
    ASSERT_EQ(read_u32(hdr + 18), (uint32_t)0);  /* 0 frames */

    remove(path);
}

UTEST(output, glif_writer_dark_flag) {
    const char *path = "/tmp/glif_test_dark.glif";
    GlifWriter gw;
    ASSERT_EQ(glif_writer_init(&gw, path, 10, 5, 8, 16, 24.0f, 1), 0);
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    FILE *f = fopen(path, "rb");
    ASSERT_TRUE(f != NULL);
    uint8_t hdr[GLIF_HEADER_SIZE];
    fread(hdr, 1, GLIF_HEADER_SIZE, f);
    fclose(f);

    ASSERT_EQ(hdr[5] & GLIF_FLAG_DARK, GLIF_FLAG_DARK);

    remove(path);
}

UTEST(output, glif_writer_frames_and_size) {
    const char *path = "/tmp/glif_test_frames.glif";
    int cols = 4, rows = 3;
    Grid grid = make_test_grid(rows, cols, 10, 20, 'X');

    GlifWriter gw;
    ASSERT_EQ(glif_writer_init(&gw, path, cols, rows, 10, 20, 30.0f, 0), 0);
    glif_writer_frame(&gw, &grid);
    glif_writer_frame(&gw, &grid);
    glif_writer_frame(&gw, &grid);
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    /* Check file size: header + 3 frames × 12 cells × 4 bytes */
    FILE *f = fopen(path, "rb");
    ASSERT_TRUE(f != NULL);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    ASSERT_EQ(size, GLIF_HEADER_SIZE + 3 * cols * rows * 4);

    /* Check frame count in header */
    rewind(f);
    uint8_t hdr[GLIF_HEADER_SIZE];
    fread(hdr, 1, GLIF_HEADER_SIZE, f);
    ASSERT_EQ(read_u32(hdr + 18), (uint32_t)3);

    fclose(f);
    remove(path);
    free(grid.cells);
}

UTEST(output, glif_writer_frame_data_matches_grid) {
    const char *path = "/tmp/glif_test_data.glif";
    int cols = 2, rows = 2;
    Grid grid = make_test_grid(rows, cols, 10, 20, 'A');
    /* Set distinct values per cell */
    grid.cells[0].ch = 'H'; grid.cells[0].r = 10; grid.cells[0].g = 20; grid.cells[0].b = 30;
    grid.cells[1].ch = 'i'; grid.cells[1].r = 40; grid.cells[1].g = 50; grid.cells[1].b = 60;
    grid.cells[2].ch = '!'; grid.cells[2].r = 70; grid.cells[2].g = 80; grid.cells[2].b = 90;
    grid.cells[3].ch = '~'; grid.cells[3].r = 100; grid.cells[3].g = 110; grid.cells[3].b = 120;

    GlifWriter gw;
    ASSERT_EQ(glif_writer_init(&gw, path, cols, rows, 10, 20, 30.0f, 1), 0);
    glif_writer_frame(&gw, &grid);
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    /* Read frame data */
    FILE *f = fopen(path, "rb");
    ASSERT_TRUE(f != NULL);
    fseek(f, GLIF_HEADER_SIZE, SEEK_SET);
    uint8_t frame[16]; /* 4 cells × 4 bytes */
    ASSERT_EQ(fread(frame, 1, 16, f), (size_t)16);
    fclose(f);

    /* Verify [ch, r, g, b] per cell */
    ASSERT_EQ(frame[0], 'H'); ASSERT_EQ(frame[1], 10); ASSERT_EQ(frame[2], 20); ASSERT_EQ(frame[3], 30);
    ASSERT_EQ(frame[4], 'i'); ASSERT_EQ(frame[5], 40); ASSERT_EQ(frame[6], 50); ASSERT_EQ(frame[7], 60);
    ASSERT_EQ(frame[8], '!'); ASSERT_EQ(frame[9], 70); ASSERT_EQ(frame[10], 80); ASSERT_EQ(frame[11], 90);
    ASSERT_EQ(frame[12], '~'); ASSERT_EQ(frame[13], 100); ASSERT_EQ(frame[14], 110); ASSERT_EQ(frame[15], 120);

    remove(path);
    free(grid.cells);
}

UTEST_MAIN();
