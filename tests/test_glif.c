#include "utest.h"
#include "glif.h"
#include "output.h"
#include "grid.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Helper: create a small grid with known characters and colors */
static GlifGrid make_test_grid(int rows, int cols, int cell_w, int cell_h, char fill_ch) {
    GlifGrid grid;
    grid.rows = rows;
    grid.cols = cols;
    grid.cell_w = cell_w;
    grid.cell_h = cell_h;
    grid.cells = calloc((size_t)(rows * cols), sizeof(GlifGridCell));
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            GlifGridCell *cell = &grid.cells[r * cols + c];
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

/* Mutate grid cells based on frame number to simulate video content */
static void vary_grid(GlifGrid *grid, int frame_num) {
    int total = grid->rows * grid->cols;
    for (int i = 0; i < total; i++) {
        GlifGridCell *cell = &grid->cells[i];
        /* Vary character and color based on cell index and frame */
        cell->ch = (char)(32 + ((i + frame_num * 7) % 95));
        cell->r = (uint8_t)((i * 13 + frame_num * 37) % 256);
        cell->g = (uint8_t)((i * 17 + frame_num * 53) % 256);
        cell->b = (uint8_t)((i * 23 + frame_num * 71) % 256);
    }
}

/* Read entire file into malloc'd buffer */
static uint8_t *file_to_buffer(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

/* Flatten a grid to [ch, r, g, b] buffer for comparison */
static uint8_t *flatten_grid(const GlifGrid *grid) {
    int total = grid->rows * grid->cols;
    uint8_t *buf = malloc((size_t)total * 4);
    if (!buf) return NULL;
    for (int i = 0; i < total; i++) {
        const GlifGridCell *cell = &grid->cells[i];
        buf[i * 4]     = (uint8_t)cell->ch;
        buf[i * 4 + 1] = cell->r;
        buf[i * 4 + 2] = cell->g;
        buf[i * 4 + 3] = cell->b;
    }
    return buf;
}

/* ── v1 tests ── */

UTEST(glif_reader, open_v1) {
    const char *path = "/tmp/glif_test_reader_v1.glif";
    int cols = 4, rows = 3;
    GlifWriter gw;
    ASSERT_EQ(glif_writer_init(&gw, path, cols, rows, 10, 20, 30.0f, 0), 0);
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);
    ASSERT_EQ(gr.header.version, GLIF_VERSION_1);
    ASSERT_EQ(gr.header.cols, cols);
    ASSERT_EQ(gr.header.rows, rows);
    ASSERT_EQ(gr.header.cell_w, 10);
    ASSERT_EQ(gr.header.cell_h, 20);
    ASSERT_TRUE(gr.header.fps > 29.9f && gr.header.fps < 30.1f);
    ASSERT_EQ(gr.header.frames, (uint32_t)0);

    glif_reader_close(&gr);
    free(buf);
    remove(path);
}

UTEST(glif_reader, open_v2) {
    const char *path = "/tmp/glif_test_reader_v2.glif";
    int cols = 4, rows = 3;
    GlifWriter gw;
    ASSERT_EQ(glif_writer_init_v2(&gw, path, cols, rows, 10, 20, 24.0f, 1, 1), 0);
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);
    /* Writer now produces v3 when compression is enabled */
    ASSERT_EQ(gr.header.version, GLIF_VERSION_3);
    ASSERT_EQ(gr.header.flags & GLIF_FLAG_DARK, GLIF_FLAG_DARK);
    ASSERT_EQ(gr.header.flags & GLIF_FLAG_COMPRESSED, GLIF_FLAG_COMPRESSED);
    ASSERT_EQ(gr.header.cols, cols);
    ASSERT_EQ(gr.header.rows, rows);
    ASSERT_TRUE(gr.header.fps > 23.9f && gr.header.fps < 24.1f);
    ASSERT_EQ(gr.header.frames, (uint32_t)0);

    glif_reader_close(&gr);
    free(buf);
    remove(path);
}

UTEST(glif_reader, roundtrip_v1_single) {
    const char *path = "/tmp/glif_test_rt_v1_single.glif";
    int cols = 4, rows = 3;
    GlifGrid grid = make_test_grid(rows, cols, 10, 20, 'A');
    vary_grid(&grid, 0);
    uint8_t *expected = flatten_grid(&grid);
    ASSERT_TRUE(expected != NULL);

    GlifWriter gw;
    ASSERT_EQ(glif_writer_init(&gw, path, cols, rows, 10, 20, 30.0f, 0), 0);
    glif_writer_frame(&gw, &grid);
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);
    ASSERT_EQ(gr.header.frames, (uint32_t)1);
    ASSERT_EQ(glif_reader_decode(&gr, 0), 0);

    const uint8_t *decoded = glif_reader_frame_data(&gr);
    ASSERT_TRUE(decoded != NULL);
    ASSERT_EQ(memcmp(decoded, expected, (size_t)(cols * rows) * 4), 0);

    glif_reader_close(&gr);
    free(buf);
    free(expected);
    free(grid.cells);
    remove(path);
}

UTEST(glif_reader, roundtrip_v1_multi) {
    const char *path = "/tmp/glif_test_rt_v1_multi.glif";
    int cols = 5, rows = 4, nframes = 5;
    GlifGrid grid = make_test_grid(rows, cols, 10, 20, 'A');

    /* Store expected data for each frame */
    uint8_t *expected[5];
    GlifWriter gw;
    ASSERT_EQ(glif_writer_init(&gw, path, cols, rows, 10, 20, 30.0f, 0), 0);
    for (int i = 0; i < nframes; i++) {
        vary_grid(&grid, i);
        expected[i] = flatten_grid(&grid);
        ASSERT_TRUE(expected[i] != NULL);
        glif_writer_frame(&gw, &grid);
    }
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);
    ASSERT_EQ(gr.header.frames, (uint32_t)nframes);

    for (int i = 0; i < nframes; i++) {
        ASSERT_EQ(glif_reader_decode(&gr, (uint32_t)i), 0);
        const uint8_t *decoded = glif_reader_frame_data(&gr);
        ASSERT_TRUE(decoded != NULL);
        ASSERT_EQ(memcmp(decoded, expected[i], (size_t)(cols * rows) * 4), 0);
    }

    glif_reader_close(&gr);
    free(buf);
    for (int i = 0; i < nframes; i++) free(expected[i]);
    free(grid.cells);
    remove(path);
}

UTEST(glif_reader, roundtrip_v2_single) {
    const char *path = "/tmp/glif_test_rt_v2_single.glif";
    int cols = 4, rows = 3;
    GlifGrid grid = make_test_grid(rows, cols, 10, 20, 'A');
    vary_grid(&grid, 0);
    uint8_t *expected = flatten_grid(&grid);
    ASSERT_TRUE(expected != NULL);

    GlifWriter gw;
    ASSERT_EQ(glif_writer_init_v2(&gw, path, cols, rows, 10, 20, 30.0f, 0, 1), 0);
    glif_writer_frame(&gw, &grid);
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);
    ASSERT_EQ(gr.header.frames, (uint32_t)1);
    ASSERT_EQ(glif_reader_decode(&gr, 0), 0);

    const uint8_t *decoded = glif_reader_frame_data(&gr);
    ASSERT_TRUE(decoded != NULL);
    ASSERT_EQ(memcmp(decoded, expected, (size_t)(cols * rows) * 4), 0);

    glif_reader_close(&gr);
    free(buf);
    free(expected);
    free(grid.cells);
    remove(path);
}

UTEST(glif_reader, roundtrip_v2_multi) {
    const char *path = "/tmp/glif_test_rt_v2_multi.glif";
    int cols = 6, rows = 5, nframes = 10;
    GlifGrid grid = make_test_grid(rows, cols, 10, 20, 'A');

    uint8_t *expected[10];
    GlifWriter gw;
    ASSERT_EQ(glif_writer_init_v2(&gw, path, cols, rows, 10, 20, 30.0f, 0, 1), 0);
    for (int i = 0; i < nframes; i++) {
        vary_grid(&grid, i);
        expected[i] = flatten_grid(&grid);
        ASSERT_TRUE(expected[i] != NULL);
        glif_writer_frame(&gw, &grid);
    }
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);
    ASSERT_EQ(gr.header.frames, (uint32_t)nframes);

    for (int i = 0; i < nframes; i++) {
        ASSERT_EQ(glif_reader_decode(&gr, (uint32_t)i), 0);
        const uint8_t *decoded = glif_reader_frame_data(&gr);
        ASSERT_TRUE(decoded != NULL);
        ASSERT_EQ(memcmp(decoded, expected[i], (size_t)(cols * rows) * 4), 0);
    }

    glif_reader_close(&gr);
    free(buf);
    for (int i = 0; i < nframes; i++) free(expected[i]);
    free(grid.cells);
    remove(path);
}

UTEST(glif_reader, random_access_v2) {
    const char *path = "/tmp/glif_test_random_v2.glif";
    int cols = 6, rows = 5, nframes = 10;
    GlifGrid grid = make_test_grid(rows, cols, 10, 20, 'A');

    uint8_t *expected[10];
    GlifWriter gw;
    ASSERT_EQ(glif_writer_init_v2(&gw, path, cols, rows, 10, 20, 30.0f, 0, 1), 0);
    for (int i = 0; i < nframes; i++) {
        vary_grid(&grid, i);
        expected[i] = flatten_grid(&grid);
        ASSERT_TRUE(expected[i] != NULL);
        glif_writer_frame(&gw, &grid);
    }
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);

    /* Decode frames in non-sequential order */
    int order[] = {5, 0, 9, 3, 7};
    for (int i = 0; i < 5; i++) {
        int f = order[i];
        ASSERT_EQ(glif_reader_decode(&gr, (uint32_t)f), 0);
        const uint8_t *decoded = glif_reader_frame_data(&gr);
        ASSERT_TRUE(decoded != NULL);
        ASSERT_EQ(memcmp(decoded, expected[f], (size_t)(cols * rows) * 4), 0);
    }

    glif_reader_close(&gr);
    free(buf);
    for (int i = 0; i < nframes; i++) free(expected[i]);
    free(grid.cells);
    remove(path);
}

/* ── Error cases ── */

UTEST(glif_reader, error_bad_magic) {
    uint8_t buf[GLIF_HEADER_SIZE] = {0};
    memcpy(buf, "NOPE", 4);
    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, sizeof(buf)), -1);
}

UTEST(glif_reader, error_truncated_header) {
    uint8_t buf[10] = {0};
    memcpy(buf, "GLIF", 4);
    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, sizeof(buf)), -1);
}

UTEST(glif_reader, error_truncated_frame) {
    const char *path = "/tmp/glif_test_truncated.glif";
    int cols = 4, rows = 3;
    GlifGrid grid = make_test_grid(rows, cols, 10, 20, 'A');

    GlifWriter gw;
    ASSERT_EQ(glif_writer_init(&gw, path, cols, rows, 10, 20, 30.0f, 0), 0);
    glif_writer_frame(&gw, &grid);
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    /* Truncate mid-frame */
    size_t truncated_len = GLIF_HEADER_SIZE + 4;
    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, truncated_len), -1);

    free(buf);
    free(grid.cells);
    remove(path);
}

UTEST(glif_reader, error_frame_out_of_range) {
    const char *path = "/tmp/glif_test_oor.glif";
    int cols = 2, rows = 2;
    GlifGrid grid = make_test_grid(rows, cols, 10, 20, 'A');

    GlifWriter gw;
    ASSERT_EQ(glif_writer_init(&gw, path, cols, rows, 10, 20, 30.0f, 0), 0);
    glif_writer_frame(&gw, &grid);
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);
    ASSERT_EQ(glif_reader_decode(&gr, 1), -1);  /* only 1 frame, index 1 is out of range */

    glif_reader_close(&gr);
    free(buf);
    free(grid.cells);
    remove(path);
}

UTEST(glif_reader, zero_frames) {
    const char *path = "/tmp/glif_test_zero.glif";
    int cols = 4, rows = 3;

    GlifWriter gw;
    ASSERT_EQ(glif_writer_init(&gw, path, cols, rows, 10, 20, 30.0f, 0), 0);
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);
    ASSERT_EQ(gr.header.frames, (uint32_t)0);
    ASSERT_EQ(glif_reader_decode(&gr, 0), -1);

    glif_reader_close(&gr);
    free(buf);
    remove(path);
}

/* ── v3 tests ── */

UTEST(glif_reader, roundtrip_v3_single) {
    const char *path = "/tmp/glif_test_rt_v3_single.glif";
    int cols = 4, rows = 3;
    GlifGrid grid = make_test_grid(rows, cols, 10, 20, 'A');
    vary_grid(&grid, 0);
    uint8_t *expected = flatten_grid(&grid);
    ASSERT_TRUE(expected != NULL);

    GlifWriter gw;
    ASSERT_EQ(glif_writer_init_v2(&gw, path, cols, rows, 10, 20, 30.0f, 0, 1), 0);
    glif_writer_frame(&gw, &grid);
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);
    ASSERT_EQ(gr.header.version, GLIF_VERSION_3);
    ASSERT_EQ(gr.header.frames, (uint32_t)1);
    ASSERT_EQ(glif_reader_decode(&gr, 0), 0);

    const uint8_t *decoded = glif_reader_frame_data(&gr);
    ASSERT_TRUE(decoded != NULL);
    ASSERT_EQ(memcmp(decoded, expected, (size_t)(cols * rows) * 4), 0);

    glif_reader_close(&gr);
    free(buf);
    free(expected);
    free(grid.cells);
    remove(path);
}

UTEST(glif_reader, roundtrip_v3_multi) {
    const char *path = "/tmp/glif_test_rt_v3_multi.glif";
    int cols = 20, rows = 15, nframes = 10;
    GlifGrid grid = make_test_grid(rows, cols, 10, 20, 'A');

    uint8_t *expected[10];
    GlifWriter gw;
    ASSERT_EQ(glif_writer_init_v2(&gw, path, cols, rows, 10, 20, 30.0f, 0, 1), 0);
    for (int i = 0; i < nframes; i++) {
        vary_grid(&grid, i);
        expected[i] = flatten_grid(&grid);
        ASSERT_TRUE(expected[i] != NULL);
        glif_writer_frame(&gw, &grid);
    }
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);
    ASSERT_EQ(gr.header.version, GLIF_VERSION_3);
    ASSERT_EQ(gr.header.frames, (uint32_t)nframes);

    for (int i = 0; i < nframes; i++) {
        ASSERT_EQ(glif_reader_decode(&gr, (uint32_t)i), 0);
        const uint8_t *decoded = glif_reader_frame_data(&gr);
        ASSERT_TRUE(decoded != NULL);
        ASSERT_EQ(memcmp(decoded, expected[i], (size_t)(cols * rows) * 4), 0);
    }

    glif_reader_close(&gr);
    free(buf);
    for (int i = 0; i < nframes; i++) free(expected[i]);
    free(grid.cells);
    remove(path);
}

UTEST(glif_reader, random_access_v3) {
    const char *path = "/tmp/glif_test_random_v3.glif";
    int cols = 20, rows = 15, nframes = 10;
    GlifGrid grid = make_test_grid(rows, cols, 10, 20, 'A');

    uint8_t *expected[10];
    GlifWriter gw;
    ASSERT_EQ(glif_writer_init_v2(&gw, path, cols, rows, 10, 20, 30.0f, 0, 1), 0);
    for (int i = 0; i < nframes; i++) {
        vary_grid(&grid, i);
        expected[i] = flatten_grid(&grid);
        ASSERT_TRUE(expected[i] != NULL);
        glif_writer_frame(&gw, &grid);
    }
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);

    /* Decode frames in non-sequential order */
    int order[] = {5, 0, 9, 3, 7};
    for (int i = 0; i < 5; i++) {
        int f = order[i];
        ASSERT_EQ(glif_reader_decode(&gr, (uint32_t)f), 0);
        const uint8_t *decoded = glif_reader_frame_data(&gr);
        ASSERT_TRUE(decoded != NULL);
        ASSERT_EQ(memcmp(decoded, expected[f], (size_t)(cols * rows) * 4), 0);
    }

    glif_reader_close(&gr);
    free(buf);
    for (int i = 0; i < nframes; i++) free(expected[i]);
    free(grid.cells);
    remove(path);
}

UTEST(glif_reader, v3_mostly_static_frames) {
    /* Test with frames that are mostly identical — should heavily favor block delta */
    const char *path = "/tmp/glif_test_v3_static.glif";
    int cols = 30, rows = 20, nframes = 5;
    GlifGrid grid = make_test_grid(rows, cols, 10, 20, 'A');

    uint8_t *expected[5];
    GlifWriter gw;
    ASSERT_EQ(glif_writer_init_v2(&gw, path, cols, rows, 10, 20, 30.0f, 0, 1), 0);
    for (int i = 0; i < nframes; i++) {
        /* Only change a few cells per frame */
        if (i > 0) {
            int idx = i * 3;
            if (idx < cols * rows) {
                grid.cells[idx].ch = (char)(33 + i);
                grid.cells[idx].r = (uint8_t)(i * 50);
            }
        }
        expected[i] = flatten_grid(&grid);
        ASSERT_TRUE(expected[i] != NULL);
        glif_writer_frame(&gw, &grid);
    }
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);
    ASSERT_EQ(gr.header.version, GLIF_VERSION_3);

    for (int i = 0; i < nframes; i++) {
        ASSERT_EQ(glif_reader_decode(&gr, (uint32_t)i), 0);
        const uint8_t *decoded = glif_reader_frame_data(&gr);
        ASSERT_TRUE(decoded != NULL);
        ASSERT_EQ(memcmp(decoded, expected[i], (size_t)(cols * rows) * 4), 0);
    }

    glif_reader_close(&gr);
    free(buf);
    for (int i = 0; i < nframes; i++) free(expected[i]);
    free(grid.cells);
    remove(path);
}

UTEST(glif_reader, backward_compat_v1_still_works) {
    /* Ensure v1 files still load correctly after v3 changes */
    const char *path = "/tmp/glif_test_compat_v1.glif";
    int cols = 4, rows = 3, nframes = 3;
    GlifGrid grid = make_test_grid(rows, cols, 10, 20, 'A');

    uint8_t *expected[3];
    GlifWriter gw;
    ASSERT_EQ(glif_writer_init(&gw, path, cols, rows, 10, 20, 30.0f, 0), 0);
    for (int i = 0; i < nframes; i++) {
        vary_grid(&grid, i);
        expected[i] = flatten_grid(&grid);
        ASSERT_TRUE(expected[i] != NULL);
        glif_writer_frame(&gw, &grid);
    }
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);
    ASSERT_EQ(gr.header.version, GLIF_VERSION_1);

    for (int i = 0; i < nframes; i++) {
        ASSERT_EQ(glif_reader_decode(&gr, (uint32_t)i), 0);
        const uint8_t *decoded = glif_reader_frame_data(&gr);
        ASSERT_TRUE(decoded != NULL);
        ASSERT_EQ(memcmp(decoded, expected[i], (size_t)(cols * rows) * 4), 0);
    }

    glif_reader_close(&gr);
    free(buf);
    for (int i = 0; i < nframes; i++) free(expected[i]);
    free(grid.cells);
    remove(path);
}

UTEST_MAIN();
