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
    MatchCache mc;
    match_cache_init(&mc);
    ASSERT_TRUE(mc.cache != NULL);
    ASSERT_GT(mc.cache_size, 0);
    match_cache_free(&mc);
}

UTEST(match, cache_free_nullifies) {
    MatchCache mc;
    match_cache_init(&mc);
    match_cache_free(&mc);
    ASSERT_TRUE(mc.cache == NULL);
}

UTEST(match, brute_force_space_for_zero) {
    /* Load real font to get a CharDatabase */
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) {
        /* Skip test if font not available */
        return;
    }

    /* Zero vector should match space (which also has zero vector) */
    Vec6 zero = vec6_zero();
    char ch = match_find(&zero, &db, NULL);
    ASSERT_EQ(ch, ' ');

    char_db_free(&db);
}

UTEST(match, cache_consistency) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    MatchCache mc;
    match_cache_init(&mc);

    /* Same input should always produce same output (cache hit) */
    Vec6 v = {{0.3f, 0.5f, 0.7f, 0.1f, 0.9f, 0.4f}};
    v = vec6_normalize(v);

    char ch1 = match_find(&v, &db, &mc);
    char ch2 = match_find(&v, &db, &mc);
    ASSERT_EQ(ch1, ch2);

    /* Result should be a printable ASCII character */
    ASSERT_GE(ch1, 32);
    ASSERT_LE(ch1, 126);

    match_cache_free(&mc);
    char_db_free(&db);
}

UTEST(match, different_vectors_can_match_different_chars) {
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    /* Two very different vectors should at least potentially match different chars */
    Vec6 a = {{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    Vec6 b = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
    a = vec6_normalize(a);
    b = vec6_normalize(b);

    char ca = match_find(&a, &db, NULL);
    char cb = match_find(&b, &db, NULL);

    /* Both should be valid printable chars */
    ASSERT_GE(ca, 32);
    ASSERT_LE(ca, 126);
    ASSERT_GE(cb, 32);
    ASSERT_LE(cb, 126);

    char_db_free(&db);
}

UTEST(match, uniform_vector_matches_consistently) {
    /* A uniform normalized vector (all equal components) should match consistently */
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    Vec6 uniform = {{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
    uniform = vec6_normalize(uniform);

    char ch1 = match_find(&uniform, &db, NULL);
    char ch2 = match_find(&uniform, &db, NULL);
    ASSERT_EQ(ch1, ch2);

    /* Must be printable ASCII */
    ASSERT_GE(ch1, 32);
    ASSERT_LE(ch1, 126);

    char_db_free(&db);
}

UTEST(match, all_matched_chars_in_printable_range) {
    /* Run match_grid on a small grid and verify all chars are in [32, 126] */
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    /* Create a small synthetic grid with various shape vectors */
    Grid grid;
    grid.rows = 2;
    grid.cols = 3;
    grid.cell_w = 10;
    grid.cell_h = 20;
    grid.cells = calloc(6, sizeof(GridCell));
    ASSERT_TRUE(grid.cells != NULL);

    /* Set various shape vectors */
    grid.cells[0].shape = vec6_normalize((Vec6){{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}});
    grid.cells[1].shape = vec6_normalize((Vec6){{0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f}});
    grid.cells[2].shape = vec6_normalize((Vec6){{0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f}});
    grid.cells[3].shape = vec6_zero();
    grid.cells[4].shape = vec6_normalize((Vec6){{0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f}});
    grid.cells[5].shape = vec6_normalize((Vec6){{0.9f, 0.1f, 0.9f, 0.1f, 0.9f, 0.1f}});

    match_grid(&grid, &db);

    for (int i = 0; i < 6; i++) {
        ASSERT_GE(grid.cells[i].ch, 32);
        ASSERT_LE(grid.cells[i].ch, 126);
    }

    free(grid.cells);
    char_db_free(&db);
}

UTEST(match, without_cache_works) {
    /* Match with NULL cache pointer should still work */
    SamplingConfig sc;
    sampling_config_init(&sc);

    CharDatabase db;
    int ret = char_db_create(&db, test_font(), 10, 20, &sc);
    if (ret != 0) return;

    Vec6 v = {{0.5f, 0.3f, 0.8f, 0.2f, 0.6f, 0.4f}};
    v = vec6_normalize(v);

    char ch = match_find(&v, &db, NULL);
    ASSERT_GE(ch, 32);
    ASSERT_LE(ch, 126);

    char_db_free(&db);
}

UTEST_MAIN();
