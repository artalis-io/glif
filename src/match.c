#include "match.h"
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#define CACHE_SIZE (1 << 20)  /* 1M entries */
#define BITS_PER 5
#define LEVELS (1 << BITS_PER)  /* 32 */

int match_cache_init(MatchCache *mc) {
    mc->cache_size = CACHE_SIZE;
    mc->cache = calloc((size_t)CACHE_SIZE, 1);
    if (!mc->cache) {
        mc->cache_size = 0;
        return -1;
    }
    return 0;
}

static unsigned int quantize_key(const Vec6 *v) {
    unsigned int key = 0;
    for (int i = 0; i < 6; i++) {
        float clamped = v->v[i];
        if (clamped < 0.0f) clamped = 0.0f;
        if (clamped > 1.0f) clamped = 1.0f;
        unsigned int q = (unsigned int)(clamped * (LEVELS - 1) + 0.5f);
        key |= (q << (i * BITS_PER));
    }
    return key % CACHE_SIZE;
}

static char brute_force_match(const Vec6 *shape, const CharDatabase *db) {
    float best_dist = 1e30f;
    char best_ch = ' ';

    for (int i = 0; i < CHAR_COUNT; i++) {
        float d = vec6_dist_sq(*shape, db->entries[i].shape);
        if (d < best_dist) {
            best_dist = d;
            best_ch = db->entries[i].ch;
        }
    }
    return best_ch;
}

char match_find(const Vec6 *shape, const CharDatabase *db, MatchCache *mc) {
    if (mc && mc->cache) {
        unsigned int key = quantize_key(shape);
        if (mc->cache[key] != 0) {
            return mc->cache[key];
        }
        char ch = brute_force_match(shape, db);
        mc->cache[key] = ch;
        return ch;
    }
    return brute_force_match(shape, db);
}

void match_grid(Grid *grid, const CharDatabase *db) {
    int ncells = grid->rows * grid->cols;
    GridCell *cells = grid->cells;

    #pragma omp parallel
    {
        /* Per-thread cache to avoid data races */
        MatchCache mc;
        if (match_cache_init(&mc) != 0) {
            mc.cache = NULL;
            mc.cache_size = 0;
        }

        #pragma omp for schedule(static)
        for (int i = 0; i < ncells; i++) {
            cells[i].ch = match_find(&cells[i].shape, db, &mc);
        }

        match_cache_free(&mc);
    }
}

void match_cache_free(MatchCache *mc) {
    if (!mc) return;
    free(mc->cache);
    mc->cache = NULL;
}
