#ifndef GLIF_FONT_H
#define GLIF_FONT_H

#include <stddef.h>
#include "vec6.h"
#include "sampling.h"

#define GLIF_CHAR_FIRST 32   /* space */
#define GLIF_CHAR_LAST  126  /* tilde */
#define GLIF_CHAR_COUNT (GLIF_CHAR_LAST - GLIF_CHAR_FIRST + 1) /* 95 */

typedef struct {
    char ch;
    GlifVec6 shape;     /* normalized shape vector */
    uint8_t *bitmap; /* glyph bitmap (cell_w × cell_h) for PPM rendering */
} GlifCharEntry;

typedef struct {
    GlifCharEntry entries[GLIF_CHAR_COUNT];
    int cell_w;
    int cell_h;
    unsigned char *font_data;  /* raw TTF file data */
    size_t font_data_len;      /* byte length of font_data */
    int owns_font_data;        /* 1 = file-loaded, 0 = external buffer */
} GlifCharDatabase;

/* Load font and precompute character shape vectors.
 * cell_w, cell_h define the rasterization size.
 * Returns 0 on success, -1 on failure. */
int glif_char_db_create(GlifCharDatabase *db, const char *font_path,
                   int cell_w, int cell_h, const GlifSamplingConfig *sc);

/* Create from font data already in memory (no copy). */
int glif_char_db_create_from_memory(GlifCharDatabase *db, const unsigned char *font_data,
                               size_t font_len, int cell_w, int cell_h,
                               const GlifSamplingConfig *sc);

/* Rasterize all glyphs at a scaled size for high-res PPM output.
 * Returns array of GLIF_CHAR_COUNT bitmaps, each (cell_w*scale × cell_h*scale).
 * Caller must free each bitmap and the array itself. */
uint8_t **glif_char_db_render_bitmaps(const GlifCharDatabase *db, int scale);

void glif_char_db_free(GlifCharDatabase *db);

#endif
