#include "utest.h"
#include "vec6.h"
#include "font.h"
#include "match.h"
#include <math.h>
#include <stdlib.h>

static const char *test_font(void) {
    const char *env = getenv("GLIF_TEST_FONT");
    return env ? env : "fonts/SFNSMono.ttf";
}

UTEST(match, cache_init_free) {
    GlifMatchCache mc;
    glif_match_cache_init(&mc);
    ASSERT_TRUE(mc.cache != NULL);
    ASSERT_GT(mc.cache_size, 0);
    glif_match_cache_free(&mc);
}

UTEST(match, cache_free_nullifies) {
    GlifMatchCache mc;
    glif_match_cache_init(&mc);
    glif_match_cache_free(&mc);
    ASSERT_TRUE(mc.cache == NULL);
}

UTEST(match, brute_force_space_for_zero) {
    /* Load real font to get a GlifCharDatabase */
    GlifSamplingConfig sc;
    glif_sampling_config_init(&sc);

    GlifCharDatabase db;
    int ret = glif_char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) {
        /* Skip test if font not available */
        return;
    }

    /* Zero vector should match space (which also has zero vector) */
    GlifVec6 zero = glif_vec6_zero();
    char ch = glif_match_find(&zero, &db, NULL);
    ASSERT_EQ(ch, ' ');

    glif_char_db_free(&db);
}

UTEST(match, cache_consistency) {
    GlifSamplingConfig sc;
    glif_sampling_config_init(&sc);

    GlifCharDatabase db;
    int ret = glif_char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    GlifMatchCache mc;
    glif_match_cache_init(&mc);

    /* Same input should always produce same output (cache hit) */
    GlifVec6 v = {{0.3f, 0.5f, 0.7f, 0.1f, 0.9f, 0.4f}};
    v = glif_vec6_normalize(v);

    char ch1 = glif_match_find(&v, &db, &mc);
    char ch2 = glif_match_find(&v, &db, &mc);
    ASSERT_EQ(ch1, ch2);

    /* Result should be a printable ASCII character */
    ASSERT_GE(ch1, 32);
    ASSERT_LE(ch1, 126);

    glif_match_cache_free(&mc);
    glif_char_db_free(&db);
}

UTEST(match, different_vectors_can_match_different_chars) {
    GlifSamplingConfig sc;
    glif_sampling_config_init(&sc);

    GlifCharDatabase db;
    int ret = glif_char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    /* Two very different vectors should at least potentially match different chars */
    GlifVec6 a = {{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    GlifVec6 b = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
    a = glif_vec6_normalize(a);
    b = glif_vec6_normalize(b);

    char ca = glif_match_find(&a, &db, NULL);
    char cb = glif_match_find(&b, &db, NULL);

    /* Both should be valid printable chars */
    ASSERT_GE(ca, 32);
    ASSERT_LE(ca, 126);
    ASSERT_GE(cb, 32);
    ASSERT_LE(cb, 126);

    glif_char_db_free(&db);
}

UTEST(match, uniform_vector_matches_consistently) {
    /* A uniform normalized vector (all equal components) should match consistently */
    GlifSamplingConfig sc;
    glif_sampling_config_init(&sc);

    GlifCharDatabase db;
    int ret = glif_char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    GlifVec6 uniform = {{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
    uniform = glif_vec6_normalize(uniform);

    char ch1 = glif_match_find(&uniform, &db, NULL);
    char ch2 = glif_match_find(&uniform, &db, NULL);
    ASSERT_EQ(ch1, ch2);

    /* Must be printable ASCII */
    ASSERT_GE(ch1, 32);
    ASSERT_LE(ch1, 126);

    glif_char_db_free(&db);
}

UTEST(match, all_matched_chars_in_printable_range) {
    /* Run glif_match_grid on a small grid and verify all chars are in [32, 126] */
    GlifSamplingConfig sc;
    glif_sampling_config_init(&sc);

    GlifCharDatabase db;
    int ret = glif_char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    /* Create a small synthetic grid with various shape vectors */
    GlifGrid grid;
    grid.rows = 2;
    grid.cols = 3;
    grid.cell_w = 10;
    grid.cell_h = 20;
    grid.cells = calloc(6, sizeof(GlifGridCell));
    ASSERT_TRUE(grid.cells != NULL);

    /* Set various shape vectors */
    grid.cells[0].shape = glif_vec6_normalize((GlifVec6){{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}});
    grid.cells[1].shape = glif_vec6_normalize((GlifVec6){{0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f}});
    grid.cells[2].shape = glif_vec6_normalize((GlifVec6){{0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f}});
    grid.cells[3].shape = glif_vec6_zero();
    grid.cells[4].shape = glif_vec6_normalize((GlifVec6){{0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f}});
    grid.cells[5].shape = glif_vec6_normalize((GlifVec6){{0.9f, 0.1f, 0.9f, 0.1f, 0.9f, 0.1f}});

    glif_match_grid(&grid, &db);

    for (int i = 0; i < 6; i++) {
        ASSERT_GE(grid.cells[i].ch, 32);
        ASSERT_LE(grid.cells[i].ch, 126);
    }

    free(grid.cells);
    glif_char_db_free(&db);
}

UTEST(match, without_cache_works) {
    /* Match with NULL cache pointer should still work */
    GlifSamplingConfig sc;
    glif_sampling_config_init(&sc);

    GlifCharDatabase db;
    int ret = glif_char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    GlifVec6 v = {{0.5f, 0.3f, 0.8f, 0.2f, 0.6f, 0.4f}};
    v = glif_vec6_normalize(v);

    char ch = glif_match_find(&v, &db, NULL);
    ASSERT_GE(ch, 32);
    ASSERT_LE(ch, 126);

    glif_char_db_free(&db);
}

UTEST_MAIN();
