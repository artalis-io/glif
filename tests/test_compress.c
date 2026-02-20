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

/* ---- Deflate tests ---- */

UTEST(compress, deflate_roundtrip_uniform) {
    int cells = 100;
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_uniform(cur, cells, 'A', 10, 20, 30);

    size_t cap = glif_compress_deflate_bound(cells);
    uint8_t *enc = malloc(cap);

    int enc_size = glif_compress_deflate_encode(cur, cells, enc, cap);
    ASSERT_GT(enc_size, 0);
    /* Uniform data should compress very well */
    ASSERT_LT(enc_size, cells * 4);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_deflate_decode(enc, (size_t)enc_size, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(enc); free(dec);
}

UTEST(compress, deflate_roundtrip_unique) {
    int cells = 200;
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_unique(cur, cells);

    size_t cap = glif_compress_deflate_bound(cells);
    uint8_t *enc = malloc(cap);

    int enc_size = glif_compress_deflate_encode(cur, cells, enc, cap);
    ASSERT_GT(enc_size, 0);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_deflate_decode(enc, (size_t)enc_size, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(enc); free(dec);
}

UTEST(compress, deflate_roundtrip_random) {
    int cells = 500;
    uint8_t *cur = malloc((size_t)cells * 4);
    for (int i = 0; i < cells * 4; i++)
        cur[i] = (uint8_t)((i * 37 + 13) & 0xFF);

    size_t cap = glif_compress_deflate_bound(cells);
    uint8_t *enc = malloc(cap);
    int enc_size = glif_compress_deflate_encode(cur, cells, enc, cap);
    ASSERT_GT(enc_size, 0);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_deflate_decode(enc, (size_t)enc_size, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(enc); free(dec);
}

/* ---- Delta+Deflate tests ---- */

UTEST(compress, delta_deflate_identical_frames) {
    int cells = 100;
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_uniform(cur, cells, 'X', 128, 64, 32);

    size_t cap = glif_compress_deflate_bound(cells);
    uint8_t *work = malloc((size_t)cells * 4);
    uint8_t *enc = malloc(cap);

    int enc_size = glif_compress_delta_deflate_encode(cur, cur, cells, work, enc, cap);
    ASSERT_GT(enc_size, 0);
    /* XOR = all zeros -> should compress to near-nothing */
    ASSERT_LT(enc_size, 30);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_delta_deflate_decode(enc, (size_t)enc_size, cur, work, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(work); free(enc); free(dec);
}

UTEST(compress, delta_deflate_single_change) {
    int cells = 100;
    uint8_t *prev = malloc((size_t)cells * 4);
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_uniform(prev, cells, 'A', 10, 20, 30);
    memcpy(cur, prev, (size_t)cells * 4);
    cur[50 * 4] = 'Z';
    cur[50 * 4 + 1] = 255;

    size_t cap = glif_compress_deflate_bound(cells);
    uint8_t *work = malloc((size_t)cells * 4);
    uint8_t *enc = malloc(cap);

    int enc_size = glif_compress_delta_deflate_encode(cur, prev, cells, work, enc, cap);
    ASSERT_GT(enc_size, 0);
    /* Mostly zeros with small change -> very small */
    ASSERT_LT(enc_size, 50);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_delta_deflate_decode(enc, (size_t)enc_size, prev, work, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(prev); free(cur); free(work); free(enc); free(dec);
}

UTEST(compress, delta_deflate_roundtrip_random) {
    int cells = 500;
    uint8_t *prev = malloc((size_t)cells * 4);
    uint8_t *cur = malloc((size_t)cells * 4);
    for (int i = 0; i < cells * 4; i++) {
        prev[i] = (uint8_t)((i * 37 + 13) & 0xFF);
        cur[i] = (uint8_t)((i * 41 + 7) & 0xFF);
    }

    size_t cap = glif_compress_deflate_bound(cells);
    uint8_t *work = malloc((size_t)cells * 4);
    uint8_t *enc = malloc(cap);
    int enc_size = glif_compress_delta_deflate_encode(cur, prev, cells, work, enc, cap);
    ASSERT_GT(enc_size, 0);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_delta_deflate_decode(enc, (size_t)enc_size, prev, work, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(prev); free(cur); free(work); free(enc); free(dec);
}

/* ---- Filtered deflate tests ---- */

UTEST(compress, filtered_deflate_roundtrip_uniform) {
    int cols = 10, rows = 10;
    int cells = cols * rows;
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_uniform(cur, cells, 'A', 10, 20, 30);

    size_t cap = glif_compress_filtered_deflate_bound(cols, rows);
    uint8_t *enc = malloc(cap);

    int enc_size = glif_compress_filtered_deflate_encode(cur, cols, rows, enc, cap);
    ASSERT_GT(enc_size, 0);
    ASSERT_LT(enc_size, cells * 4);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_filtered_deflate_decode(enc, (size_t)enc_size, dec, cols, rows), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(enc); free(dec);
}

UTEST(compress, filtered_deflate_roundtrip_random) {
    int cols = 20, rows = 15;
    int cells = cols * rows;
    uint8_t *cur = malloc((size_t)cells * 4);
    for (int i = 0; i < cells * 4; i++)
        cur[i] = (uint8_t)((i * 37 + 13) & 0xFF);

    size_t cap = glif_compress_filtered_deflate_bound(cols, rows);
    uint8_t *enc = malloc(cap);
    int enc_size = glif_compress_filtered_deflate_encode(cur, cols, rows, enc, cap);
    ASSERT_GT(enc_size, 0);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_filtered_deflate_decode(enc, (size_t)enc_size, dec, cols, rows), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(enc); free(dec);
}

UTEST(compress, delta_filtered_deflate_identical) {
    int cols = 10, rows = 10;
    int cells = cols * rows;
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_uniform(cur, cells, 'X', 128, 64, 32);

    size_t cap = glif_compress_filtered_deflate_bound(cols, rows);
    uint8_t *enc = malloc(cap);

    int enc_size = glif_compress_delta_filtered_deflate_encode(cur, cur, cols, rows, enc, cap);
    ASSERT_GT(enc_size, 0);
    ASSERT_LT(enc_size, 30);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_delta_filtered_deflate_decode(enc, (size_t)enc_size, cur, dec, cols, rows), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(enc); free(dec);
}

UTEST(compress, delta_filtered_deflate_roundtrip_random) {
    int cols = 20, rows = 15;
    int cells = cols * rows;
    uint8_t *prev = malloc((size_t)cells * 4);
    uint8_t *cur = malloc((size_t)cells * 4);
    for (int i = 0; i < cells * 4; i++) {
        prev[i] = (uint8_t)((i * 37 + 13) & 0xFF);
        cur[i] = (uint8_t)((i * 41 + 7) & 0xFF);
    }

    size_t cap = glif_compress_filtered_deflate_bound(cols, rows);
    uint8_t *enc = malloc(cap);
    int enc_size = glif_compress_delta_filtered_deflate_encode(cur, prev, cols, rows, enc, cap);
    ASSERT_GT(enc_size, 0);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_delta_filtered_deflate_decode(enc, (size_t)enc_size, prev, dec, cols, rows), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(prev); free(cur); free(enc); free(dec);
}

/* ---- Palette deflate tests ---- */

UTEST(compress, palette_deflate_few_colors) {
    int cells = 100;
    uint8_t *cur = malloc((size_t)cells * 4);
    /* 3 colors: (10,20,30), (40,50,60), (70,80,90) */
    for (int i = 0; i < cells; i++) {
        cur[i * 4] = (uint8_t)(32 + (i % 95));
        int color = i % 3;
        cur[i * 4 + 1] = (uint8_t)(10 + color * 30);
        cur[i * 4 + 2] = (uint8_t)(20 + color * 30);
        cur[i * 4 + 3] = (uint8_t)(30 + color * 30);
    }

    size_t cap = glif_compress_palette_deflate_bound(cells);
    uint8_t *enc = malloc(cap);
    int enc_size = glif_compress_palette_deflate_encode(cur, cells, enc, cap);
    ASSERT_GT(enc_size, 0);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_palette_deflate_decode(enc, (size_t)enc_size, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(enc); free(dec);
}

UTEST(compress, palette_deflate_too_many_colors) {
    /* > 256 unique colors should fail */
    int cells = 300;
    uint8_t *cur = malloc((size_t)cells * 4);
    for (int i = 0; i < cells; i++) {
        cur[i * 4] = 'A';
        cur[i * 4 + 1] = (uint8_t)(i % 256);
        cur[i * 4 + 2] = (uint8_t)(i / 256);
        cur[i * 4 + 3] = 0;
    }

    size_t cap = glif_compress_palette_deflate_bound(cells);
    uint8_t *enc = malloc(cap);
    int enc_size = glif_compress_palette_deflate_encode(cur, cells, enc, cap);
    ASSERT_EQ(enc_size, -1);

    free(cur); free(enc);
}

UTEST(compress, palette_deflate_single_color) {
    int cells = 50;
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_uniform(cur, cells, 'Z', 255, 128, 0);

    size_t cap = glif_compress_palette_deflate_bound(cells);
    uint8_t *enc = malloc(cap);
    int enc_size = glif_compress_palette_deflate_encode(cur, cells, enc, cap);
    ASSERT_GT(enc_size, 0);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_palette_deflate_decode(enc, (size_t)enc_size, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(enc); free(dec);
}

/* ---- Per-plane deflate tests ---- */

UTEST(compress, planar_deflate_roundtrip_uniform) {
    int cells = 100;
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_uniform(cur, cells, 'A', 10, 20, 30);

    size_t cap = glif_compress_planar_deflate_bound(cells);
    uint8_t *enc = malloc(cap);
    int enc_size = glif_compress_planar_deflate_encode(cur, cells, enc, cap);
    ASSERT_GT(enc_size, 0);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_planar_deflate_decode(enc, (size_t)enc_size, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(enc); free(dec);
}

UTEST(compress, planar_deflate_roundtrip_random) {
    int cells = 500;
    uint8_t *cur = malloc((size_t)cells * 4);
    for (int i = 0; i < cells * 4; i++)
        cur[i] = (uint8_t)((i * 37 + 13) & 0xFF);

    size_t cap = glif_compress_planar_deflate_bound(cells);
    uint8_t *enc = malloc(cap);
    int enc_size = glif_compress_planar_deflate_encode(cur, cells, enc, cap);
    ASSERT_GT(enc_size, 0);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_planar_deflate_decode(enc, (size_t)enc_size, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(enc); free(dec);
}

UTEST(compress, delta_planar_deflate_identical) {
    int cells = 100;
    uint8_t *cur = malloc((size_t)cells * 4);
    fill_uniform(cur, cells, 'X', 128, 64, 32);

    size_t cap = glif_compress_planar_deflate_bound(cells);
    uint8_t *enc = malloc(cap);
    int enc_size = glif_compress_delta_planar_deflate_encode(cur, cur, cells, enc, cap);
    ASSERT_GT(enc_size, 0);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_delta_planar_deflate_decode(enc, (size_t)enc_size, cur, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(cur); free(enc); free(dec);
}

UTEST(compress, delta_planar_deflate_roundtrip_random) {
    int cells = 500;
    uint8_t *prev = malloc((size_t)cells * 4);
    uint8_t *cur = malloc((size_t)cells * 4);
    for (int i = 0; i < cells * 4; i++) {
        prev[i] = (uint8_t)((i * 37 + 13) & 0xFF);
        cur[i] = (uint8_t)((i * 41 + 7) & 0xFF);
    }

    size_t cap = glif_compress_planar_deflate_bound(cells);
    uint8_t *enc = malloc(cap);
    int enc_size = glif_compress_delta_planar_deflate_encode(cur, prev, cells, enc, cap);
    ASSERT_GT(enc_size, 0);

    uint8_t *dec = malloc((size_t)cells * 4);
    ASSERT_EQ(glif_compress_delta_planar_deflate_decode(enc, (size_t)enc_size, prev, dec, cells), 0);
    ASSERT_EQ(memcmp(cur, dec, (size_t)cells * 4), 0);

    free(prev); free(cur); free(enc); free(dec);
}

UTEST_MAIN();
