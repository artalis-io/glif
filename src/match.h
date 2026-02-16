#ifndef MATCH_H
#define MATCH_H

#include "grid.h"
#include "font.h"

typedef struct {
    /* 30-bit quantized cache: 5 bits per component × 6 components */
    char *cache;    /* CACHE_SIZE entries, 0 = empty */
    int cache_size;
} MatchCache;

/* Initialize match cache. Returns 0 on success, -1 on allocation failure. */
int match_cache_init(MatchCache *mc);

/* Find nearest character for a shape vector. Uses cache if available. */
char match_find(const Vec6 *shape, const CharDatabase *db, MatchCache *mc);

/* Match all grid cells to their nearest ASCII characters. */
void match_grid(Grid *grid, const CharDatabase *db);

void match_cache_free(MatchCache *mc);

#endif
