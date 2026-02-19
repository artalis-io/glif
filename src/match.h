#ifndef GLIF_MATCH_H
#define GLIF_MATCH_H

#include "grid.h"
#include "font.h"

typedef struct {
    /* 30-bit quantized cache: 5 bits per component × 6 components */
    char *cache;    /* CACHE_SIZE entries, 0 = empty */
    int cache_size;
} GlifMatchCache;

/* Initialize match cache. Returns 0 on success, -1 on allocation failure. */
int glif_match_cache_init(GlifMatchCache *mc);

/* Find nearest character for a shape vector. Uses cache if available. */
char glif_match_find(const GlifVec6 *shape, const GlifCharDatabase *db, GlifMatchCache *mc);

/* Match all grid cells to their nearest ASCII characters. */
void glif_match_grid(GlifGrid *grid, const GlifCharDatabase *db);

void glif_match_cache_free(GlifMatchCache *mc);

#endif
