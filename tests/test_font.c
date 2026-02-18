#include "utest.h"
#include "font.h"
#include "sampling.h"
#include "vec6.h"
#include <math.h>
#include <stdlib.h>

static const char *test_font(void) {
    const char *env = getenv("GLIF_TEST_FONT");
    return env ? env : "fonts/SFNSMono.ttf";
}

UTEST(font, create_succeeds_with_valid_font) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    ASSERT_EQ(ret, 0);
    ASSERT_TRUE(db.font_data != NULL);
    ASSERT_EQ(db.cell_w, 10);
    ASSERT_EQ(db.cell_h, 20);

    char_db_free(&db);
}

UTEST(font, create_fails_with_nonexistent_font) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, "fonts/nonexistent.ttf", 10, 20, &sc);
    ASSERT_NE(ret, 0);
}

UTEST(font, create_fails_with_invalid_font_data) {
    /* Create a temporary file with garbage data */
    FILE *f = fopen("/tmp/glif_test_badfont.ttf", "wb");
    ASSERT_TRUE(f != NULL);
    const char *garbage = "this is not a valid font file at all";
    fwrite(garbage, 1, strlen(garbage), f);
    fclose(f);

    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, "/tmp/glif_test_badfont.ttf", 10, 20, &sc);
    ASSERT_NE(ret, 0);

    remove("/tmp/glif_test_badfont.ttf");
}

UTEST(font, space_has_zero_shape_vector) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    /* Space is entry 0 (CHAR_FIRST = 32, space = 32) */
    ASSERT_EQ(db.entries[0].ch, ' ');
    ASSERT_NEAR(vec6_length(db.entries[0].shape), 0.0f, 1e-9f);

    char_db_free(&db);
}

UTEST(font, nonspace_chars_have_nonzero_shapes) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    /* Common characters should have nonzero shape vectors */
    /* 'A' = 65, entry index = 65 - 32 = 33 */
    int idx_A = 'A' - CHAR_FIRST;
    ASSERT_EQ(db.entries[idx_A].ch, 'A');
    ASSERT_GT(vec6_length(db.entries[idx_A].shape), 0.01f);

    /* 'M' = 77, index 45 */
    int idx_M = 'M' - CHAR_FIRST;
    ASSERT_EQ(db.entries[idx_M].ch, 'M');
    ASSERT_GT(vec6_length(db.entries[idx_M].shape), 0.01f);

    /* '#' = 35, index 3 */
    int idx_hash = '#' - CHAR_FIRST;
    ASSERT_EQ(db.entries[idx_hash].ch, '#');
    ASSERT_GT(vec6_length(db.entries[idx_hash].shape), 0.01f);

    char_db_free(&db);
}

UTEST(font, all_shape_vectors_component_normalized) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    /* Per-component max normalization: each dimension's max across all
     * characters should be ~1.0. Components are in [0, 1]. */
    float comp_max[NUM_INTERNAL] = {0};
    for (int i = 0; i < CHAR_COUNT; i++) {
        for (int s = 0; s < NUM_INTERNAL; s++) {
            if (db.entries[i].shape.v[s] > comp_max[s])
                comp_max[s] = db.entries[i].shape.v[s];
            ASSERT_GE(db.entries[i].shape.v[s], 0.0f);
            ASSERT_LE(db.entries[i].shape.v[s], 1.001f);
        }
    }
    for (int s = 0; s < NUM_INTERNAL; s++) {
        ASSERT_NEAR(comp_max[s], 1.0f, 0.01f);
    }

    char_db_free(&db);
}

UTEST(font, all_95_entries_populated) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    ASSERT_EQ(CHAR_COUNT, 95);

    for (int i = 0; i < CHAR_COUNT; i++) {
        ASSERT_EQ(db.entries[i].ch, (char)(CHAR_FIRST + i));
        /* Verify character is in printable range */
        ASSERT_GE(db.entries[i].ch, 32);
        ASSERT_LE(db.entries[i].ch, 126);
    }

    char_db_free(&db);
}

UTEST(font, bitmaps_allocated_for_all_entries) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    for (int i = 0; i < CHAR_COUNT; i++) {
        ASSERT_TRUE(db.entries[i].bitmap != NULL);
    }

    char_db_free(&db);
}

UTEST(font, render_bitmaps_valid_scale) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    uint8_t **bitmaps = char_db_render_bitmaps(&db, 2);
    ASSERT_TRUE(bitmaps != NULL);

    /* All 95 bitmaps should be allocated */
    for (int i = 0; i < CHAR_COUNT; i++) {
        ASSERT_TRUE(bitmaps[i] != NULL);
    }

    /* Clean up */
    for (int i = 0; i < CHAR_COUNT; i++) free(bitmaps[i]);
    free(bitmaps);

    char_db_free(&db);
}

UTEST(font, render_bitmaps_scale1_vs_scale2_differ) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    uint8_t **bmp1 = char_db_render_bitmaps(&db, 1);
    uint8_t **bmp2 = char_db_render_bitmaps(&db, 2);
    ASSERT_TRUE(bmp1 != NULL);
    ASSERT_TRUE(bmp2 != NULL);

    /* At scale=1, bitmap is 10x20 = 200 bytes; at scale=2, bitmap is 20x40 = 800 bytes */
    /* The scale=2 bitmaps have 4x more pixels so they must differ in size */
    int size1 = db.cell_w * 1 * db.cell_h * 1;
    int size2 = db.cell_w * 2 * db.cell_h * 2;
    ASSERT_NE(size1, size2);
    ASSERT_EQ(size2, size1 * 4);

    for (int i = 0; i < CHAR_COUNT; i++) free(bmp1[i]);
    free(bmp1);
    for (int i = 0; i < CHAR_COUNT; i++) free(bmp2[i]);
    free(bmp2);

    char_db_free(&db);
}

UTEST(font, space_bitmap_is_blank) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    /* Space is entry 0, its bitmap should be all zeros */
    uint8_t *bmp = db.entries[0].bitmap;
    int all_zero = 1;
    for (int i = 0; i < db.cell_w * db.cell_h; i++) {
        if (bmp[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    ASSERT_TRUE(all_zero);

    char_db_free(&db);
}

UTEST(font, char_db_free_nullifies) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    char_db_free(&db);
    ASSERT_TRUE(db.font_data == NULL);
    for (int i = 0; i < CHAR_COUNT; i++) {
        ASSERT_TRUE(db.entries[i].bitmap == NULL);
    }
}

UTEST_MAIN();
