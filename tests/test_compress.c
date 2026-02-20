#include "utest.h"
#include "compress.h"
#include <stdlib.h>
#include <string.h>

/* Helper: fill flat buffer with uniform cell data */
static void fill_uniform(uint8_t *buf, int cells, uint8_t ch, uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < cells; i++) {
        buf[i * 4]     = ch;
        buf[i * 4 + 1] = r;
        buf[i * 4 + 2] = g;
        buf[i * 4 + 3] = b;
    }
}

/* Helper: fill with unique per-cell data */
static void fill_unique(uint8_t *buf, int cells) {
    for (int i = 0; i < cells; i++) {
        buf[i * 4]     = (uint8_t)(i & 0xFF);
        buf[i * 4 + 1] = (uint8_t)((i + 1) & 0xFF);
        buf[i * 4 + 2] = (uint8_t)((i + 2) & 0xFF);
        buf[i * 4 + 3] = (uint8_t)((i + 3) & 0xFF);
    }
}

/* ---- RLE tests ---- */

UTEST(compress, rle_uniform_frame) {
    int cells = 100;
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_uniform(cur, cells, 'A', 10, 20, 30);

    size_t cap = glif_compress_rle_bound(cells);
    uint8_t *enc = malloc(cap);

    int enc_size = glif_compress_rle_encode(cur, cells, enc, cap);
    ASSERT_GT(enc_size, 0);
    /* 100 cells uniform = 1 run of 100 = 5 bytes */
    ASSERT_EQ(enc_size, 5);

    /* Decode and verify roundtrip */
    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_rle_decode(enc, (size_t)enc_size, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(enc); free(dec);
}

UTEST(compress, rle_all_unique) {
    int cells = 50;
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_unique(cur, cells);

    size_t cap = glif_compress_rle_bound(cells);
    uint8_t *enc = malloc(cap);

    int enc_size = glif_compress_rle_encode(cur, cells, enc, cap);
    ASSERT_GT(enc_size, 0);
    /* Worst case: each cell is a run of 1 = 5 bytes each = 250 bytes */
    ASSERT_EQ(enc_size, cells * 5);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_rle_decode(enc, (size_t)enc_size, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(enc); free(dec);
}

UTEST(compress, rle_runs_over_255) {
    int cells = 600;
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_uniform(cur, cells, 'Z', 255, 128, 0);

    size_t cap = glif_compress_rle_bound(cells);
    uint8_t *enc = malloc(cap);

    int enc_size = glif_compress_rle_encode(cur, cells, enc, cap);
    ASSERT_GT(enc_size, 0);
    /* 600 cells = 255 + 255 + 90 = 3 runs = 15 bytes */
    ASSERT_EQ(enc_size, 15);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_rle_decode(enc, (size_t)enc_size, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(enc); free(dec);
}

UTEST(compress, rle_single_cell) {
    int cells = 1;
    uint8_t cur[4] = { 'X', 100, 200, 50 };

    uint8_t enc[5];
    int enc_size = glif_compress_rle_encode(cur, cells, enc, sizeof(enc));
    ASSERT_EQ(enc_size, 5);
    ASSERT_EQ(enc[0], 1);  /* count */
    ASSERT_EQ(enc[1], 'X');
    ASSERT_EQ(enc[2], 100);

    uint8_t dec[4];
    ASSERT_EQ(glif_compress_rle_decode(enc, (size_t)enc_size, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, 4), 0);
}

UTEST(compress, rle_capacity_zero_fails) {
    uint8_t cur[4] = { 'A', 0, 0, 0 };
    uint8_t enc[1];
    ASSERT_EQ(glif_compress_rle_encode(cur, 1, enc, 0), -1);
}

UTEST(compress, rle_decode_truncated_fails) {
    /* 3 bytes is not enough for a complete run (needs 5) */
    uint8_t bad[3] = { 1, 'A', 0 };
    uint8_t dec[4];
    ASSERT_EQ(glif_compress_rle_decode(bad, 3, dec, 1), -1);
}

UTEST(compress, rle_decode_cell_count_mismatch_fails) {
    /* Encode 1 cell, try to decode as 2 cells */
    uint8_t enc[5] = { 1, 'A', 10, 20, 30 };
    uint8_t dec[8];
    ASSERT_NE(glif_compress_rle_decode(enc, 5, dec, 2), 0);
}

/* ---- Delta tests ---- */

UTEST(compress, delta_no_changes) {
    int cells = 10;
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_uniform(cur, cells, 'B', 50, 60, 70);

    size_t cap = glif_compress_delta_bound(cells);
    uint8_t *enc = malloc(cap);

    int enc_size = glif_compress_delta_encode(cur, cur, cells, enc, cap);
    ASSERT_GT(enc_size, 0);
    /* 0 changes = just 4 bytes (count) */
    ASSERT_EQ(enc_size, 4);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_delta_decode(enc, (size_t)enc_size, cur, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(enc); free(dec);
}

UTEST(compress, delta_one_change) {
    int cells = 10;
    uint8_t *prev = malloc((size_t)cells * 4);
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_uniform(prev, cells, 'A', 10, 20, 30);
    memcpy(cur, prev, (size_t)cells * 4);
    cur[5 * 4] = 'Z';
    cur[5 * 4 + 1] = 255;

    size_t cap = glif_compress_delta_bound(cells);
    uint8_t *enc = malloc(cap);

    int enc_size = glif_compress_delta_encode(cur, prev, cells, enc, cap);
    /* 1 change = 4 + 6 = 10 bytes */
    ASSERT_EQ(enc_size, 10);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_delta_decode(enc, (size_t)enc_size, prev, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(prev); free(cur); free(enc); free(dec);
}

UTEST(compress, delta_all_changed) {
    int cells = 20;
    uint8_t *prev = malloc((size_t)cells * 4);
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_uniform(prev, cells, 'A', 0, 0, 0);
    fill_unique(cur, cells);

    size_t cap = glif_compress_delta_bound(cells);
    uint8_t *enc = malloc(cap);

    int enc_size = glif_compress_delta_encode(cur, prev, cells, enc, cap);
    /* All 20 changed = 4 + 20*6 = 124 bytes */
    ASSERT_EQ(enc_size, 124);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_delta_decode(enc, (size_t)enc_size, prev, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(prev); free(cur); free(enc); free(dec);
}

UTEST(compress, delta_capacity_zero_fails) {
    uint8_t cur[4] = { 'A', 0, 0, 0 };
    uint8_t prev[4] = { 'B', 0, 0, 0 };
    uint8_t enc[1];
    ASSERT_EQ(glif_compress_delta_encode(cur, prev, 1, enc, 0), -1);
}

UTEST(compress, delta_decode_truncated_fails) {
    uint8_t bad[2] = { 1, 0 };
    uint8_t prev[4] = { 0 };
    uint8_t dec[4];
    ASSERT_EQ(glif_compress_delta_decode(bad, 2, prev, dec, 1), -1);
}

/* ---- Delta+RLE tests ---- */

UTEST(compress, delta_rle_identical_frames) {
    int cells = 100;
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_uniform(cur, cells, 'X', 128, 64, 32);

    size_t cap = glif_compress_rle_bound(cells);
    uint8_t *work = malloc((size_t)cells * 4);
    uint8_t *enc = malloc(cap);

    int enc_size = glif_compress_delta_rle_encode(cur, cur, cells, work, enc, cap);
    ASSERT_GT(enc_size, 0);
    /* XOR of identical = all zeros, 100 zero cells = 1 run = 5 bytes */
    ASSERT_EQ(enc_size, 5);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_delta_rle_decode(enc, (size_t)enc_size, cur, work, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(work); free(enc); free(dec);
}

UTEST(compress, delta_rle_single_change) {
    int cells = 50;
    uint8_t *prev = malloc((size_t)cells * 4);
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_uniform(prev, cells, 'A', 10, 20, 30);
    memcpy(cur, prev, (size_t)cells * 4);
    cur[25 * 4] = 'Z';

    size_t cap = glif_compress_rle_bound(cells);
    uint8_t *work = malloc((size_t)cells * 4);
    uint8_t *enc = malloc(cap);

    int enc_size = glif_compress_delta_rle_encode(cur, prev, cells, work, enc, cap);
    ASSERT_GT(enc_size, 0);
    /* 25 zeros + 1 diff + 24 zeros = 3 runs = 15 bytes */
    ASSERT_EQ(enc_size, 15);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_delta_rle_decode(enc, (size_t)enc_size, prev, work, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(prev); free(cur); free(work); free(enc); free(dec);
}

UTEST(compress, delta_rle_roundtrip_random) {
    int cells = 200;
    uint8_t *prev = malloc((size_t)cells * 4);
    uint8_t *cur = malloc((size_t)cells * 4);
    /* Fill with pseudo-random data */
    for (int i = 0; i < cells * 4; i++) {
        prev[i] = (uint8_t)((i * 37 + 13) & 0xFF);
        cur[i] = (uint8_t)((i * 41 + 7) & 0xFF);
    }

    size_t cap = glif_compress_rle_bound(cells);
    uint8_t *work = malloc((size_t)cells * 4);
    uint8_t *enc = malloc(cap);

    int enc_size = glif_compress_delta_rle_encode(cur, prev, cells, work, enc, cap);
    ASSERT_GT(enc_size, 0);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_delta_rle_decode(enc, (size_t)enc_size, prev, work, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(prev); free(cur); free(work); free(enc); free(dec);
}

/* ---- Bound function tests ---- */

UTEST(compress, bound_functions) {
    ASSERT_EQ(glif_compress_rle_bound(100), (size_t)500);
    ASSERT_EQ(glif_compress_delta_bound(100), (size_t)604);
    ASSERT_EQ(glif_compress_rle_bound(1), (size_t)5);
    ASSERT_EQ(glif_compress_delta_bound(1), (size_t)10);
}

/* ---- Zero cells edge case ---- */

UTEST(compress, rle_zero_cells) {
    uint8_t enc[1];
    int enc_size = glif_compress_rle_encode(NULL, 0, enc, sizeof(enc));
    ASSERT_EQ(enc_size, 0);
}

UTEST(compress, delta_encode_large_grid_fails) {
    /* cells > 65535 should fail for delta (u16 index) */
    int cells = 70000;
    uint8_t dummy[4] = {0};
    uint8_t enc[16];
    ASSERT_EQ(glif_compress_delta_encode(dummy, dummy, cells, enc, sizeof(enc)), -1);
}

UTEST_MAIN();
