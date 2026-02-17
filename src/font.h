#ifndef FONT_H
#define FONT_H

#include <stddef.h>
#include "vec6.h"
#include "sampling.h"

#define CHAR_FIRST 32   /* space */
#define CHAR_LAST  126  /* tilde */
#define CHAR_COUNT (CHAR_LAST - CHAR_FIRST + 1) /* 95 */

typedef struct {
    char ch;
    Vec6 shape;     /* normalized shape vector */
    uint8_t *bitmap; /* glyph bitmap (cell_w × cell_h) for PPM rendering */
} CharEntry;

typedef struct {
    CharEntry entries[CHAR_COUNT];
    int cell_w;
    int cell_h;
    unsigned char *font_data;  /* raw TTF file data */
    int owns_font_data;        /* 1 = file-loaded, 0 = external buffer */
} CharDatabase;

/* Load font and precompute character shape vectors.
 * cell_w, cell_h define the rasterization size.
 * Returns 0 on success, -1 on failure. */
int char_db_create(CharDatabase *db, const char *font_path,
                   int cell_w, int cell_h, const SamplingConfig *sc);

/* Create from font data already in memory (no copy). */
int char_db_create_from_memory(CharDatabase *db, const unsigned char *font_data,
                               size_t font_len, int cell_w, int cell_h,
                               const SamplingConfig *sc);

/* Rasterize all glyphs at a scaled size for high-res PPM output.
 * Returns array of CHAR_COUNT bitmaps, each (cell_w*scale × cell_h*scale).
 * Caller must free each bitmap and the array itself. */
uint8_t **char_db_render_bitmaps(const CharDatabase *db, int scale);

void char_db_free(CharDatabase *db);

#endif
