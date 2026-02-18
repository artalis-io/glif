/* stb_rect_pack must be included BEFORE stb_truetype so that stb_truetype
   uses the real stbrp_context struct instead of its tiny fallback.
   Both implementations live here; nk_impl.c skips both via NK_NO_STB_*. */
#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "font.h"
#include "image.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static unsigned char *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 1) { fclose(f); return NULL; }
    unsigned char *data = malloc((size_t)size);
    if (!data) { fclose(f); return NULL; }
    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (out_size) *out_size = (size_t)size;
    return data;
}

/* Compute the average value within a sampling circle on a glyph bitmap. */
static float glyph_circle_average(const uint8_t *bitmap, int bw, int bh,
                                  float cx_px, float cy_px, float r_px) {
    int x0 = (int)floorf(cx_px - r_px);
    int y0 = (int)floorf(cy_px - r_px);
    int x1 = (int)ceilf(cx_px + r_px);
    int y1 = (int)ceilf(cy_px + r_px);

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= bw) x1 = bw - 1;
    if (y1 >= bh) y1 = bh - 1;

    float r2 = r_px * r_px;
    float sum = 0;
    int count = 0;

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - cx_px;
            float dy = (float)y - cy_px;
            if (dx * dx + dy * dy <= r2) {
                sum += bitmap[y * bw + x] / 255.0f;
                count++;
            }
        }
    }
    return count > 0 ? sum / (float)count : 0.0f;
}

/* Shared init: rasterize glyphs + compute shape vectors. db->font_data must be set. */
static int char_db_init_from_font(CharDatabase *db, const SamplingConfig *sc) {
    int cell_w = db->cell_w;
    int cell_h = db->cell_h;

    int font_offset = stbtt_GetFontOffsetForIndex(db->font_data, 0);
    if (font_offset < 0) return -1;

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, db->font_data, font_offset)) {
        return -1;
    }

    float scale = stbtt_ScaleForPixelHeight(&font, (float)cell_h);

    /* Rasterize each printable ASCII character */
    for (int i = 0; i < CHAR_COUNT; i++) {
        char ch = (char)(CHAR_FIRST + i);
        db->entries[i].ch = ch;

        /* Allocate a blank bitmap for this cell */
        uint8_t *bmp = calloc((size_t)cell_w * (size_t)cell_h, 1);
        if (!bmp) {
            char_db_free(db);
            return -1;
        }
        db->entries[i].bitmap = bmp;

        if (ch == ' ') {
            db->entries[i].shape = vec6_zero();
            continue;
        }

        int glyph = stbtt_FindGlyphIndex(&font, ch);
        if (glyph == 0) {
            db->entries[i].shape = vec6_zero();
            continue;
        }

        int gw, gh, gx_off, gy_off;
        uint8_t *glyph_bmp = stbtt_GetGlyphBitmap(&font, scale, scale,
                                                    glyph, &gw, &gh,
                                                    &gx_off, &gy_off);
        if (!glyph_bmp) {
            db->entries[i].shape = vec6_zero();
            continue;
        }

        int ascent, descent, line_gap;
        stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
        int baseline = (int)(ascent * scale);

        int lsb, advance;
        stbtt_GetGlyphHMetrics(&font, glyph, &advance, &lsb);
        int glyph_advance = (int)(advance * scale);
        int x_offset = (cell_w - glyph_advance) / 2 + (int)(lsb * scale);
        int y_offset = baseline + gy_off;

        for (int gy = 0; gy < gh; gy++) {
            for (int gx = 0; gx < gw; gx++) {
                int dx = gx + x_offset;
                int dy = gy + y_offset;
                if (dx >= 0 && dx < cell_w && dy >= 0 && dy < cell_h) {
                    bmp[dy * cell_w + dx] = glyph_bmp[gy * gw + gx];
                }
            }
        }

        stbtt_FreeBitmap(glyph_bmp, NULL);

        Vec6 shape;
        float cw = (float)cell_w;
        float ch_f = (float)cell_h;
        for (int s = 0; s < NUM_INTERNAL; s++) {
            float cx_px = sc->internal[s].cx * cw;
            float cy_px = sc->internal[s].cy * ch_f;
            float r_px  = sc->internal[s].r * cw;
            shape.v[s] = glyph_circle_average(bmp, cell_w, cell_h,
                                              cx_px, cy_px, r_px);
        }

        db->entries[i].shape = shape;
    }

    /* Per-component max normalization */
    float comp_max[NUM_INTERNAL] = {0};
    for (int i = 0; i < CHAR_COUNT; i++) {
        for (int s = 0; s < NUM_INTERNAL; s++) {
            if (db->entries[i].shape.v[s] > comp_max[s])
                comp_max[s] = db->entries[i].shape.v[s];
        }
    }
    for (int i = 0; i < CHAR_COUNT; i++) {
        for (int s = 0; s < NUM_INTERNAL; s++) {
            if (comp_max[s] > 1e-8f)
                db->entries[i].shape.v[s] /= comp_max[s];
        }
    }

    return 0;
}

int char_db_create(CharDatabase *db, const char *font_path,
                   int cell_w, int cell_h, const SamplingConfig *sc) {
    memset(db, 0, sizeof(*db));
    db->cell_w = cell_w;
    db->cell_h = cell_h;

    size_t font_size;
    db->font_data = read_file(font_path, &font_size);
    if (!db->font_data) {
        fprintf(stderr, "error: failed to read font file '%s'\n", font_path);
        return -1;
    }
    db->owns_font_data = 1;

    if (char_db_init_from_font(db, sc) != 0) {
        fprintf(stderr, "error: failed to parse font '%s'\n", font_path);
        return -1;
    }
    return 0;
}

int char_db_create_from_memory(CharDatabase *db, const unsigned char *font_data,
                               size_t font_len, int cell_w, int cell_h,
                               const SamplingConfig *sc) {
    (void)font_len;
    memset(db, 0, sizeof(*db));
    db->cell_w = cell_w;
    db->cell_h = cell_h;
    db->font_data = (unsigned char *)font_data;
    db->owns_font_data = 0;

    if (char_db_init_from_font(db, sc) != 0)
        return -1;
    return 0;
}

uint8_t **char_db_render_bitmaps(const CharDatabase *db, int scale) {
    if (scale < 1) scale = 1;
    if (scale > 64) scale = 64;
    int rw = db->cell_w * scale;
    int rh = db->cell_h * scale;

    uint8_t **bitmaps = calloc(CHAR_COUNT, sizeof(uint8_t *));
    if (!bitmaps) return NULL;

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, db->font_data,
                        stbtt_GetFontOffsetForIndex(db->font_data, 0))) {
        free(bitmaps);
        return NULL;
    }

    float fscale = stbtt_ScaleForPixelHeight(&font, (float)rh);
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    int baseline = (int)(ascent * fscale);

    for (int i = 0; i < CHAR_COUNT; i++) {
        bitmaps[i] = calloc((size_t)rw * (size_t)rh, 1);
        if (!bitmaps[i]) goto fail;

        char ch = (char)(CHAR_FIRST + i);
        if (ch == ' ') continue;

        int glyph = stbtt_FindGlyphIndex(&font, ch);
        if (glyph == 0) continue;

        int gw, gh, gx_off, gy_off;
        uint8_t *glyph_bmp = stbtt_GetGlyphBitmap(&font, fscale, fscale,
                                                    glyph, &gw, &gh,
                                                    &gx_off, &gy_off);
        if (!glyph_bmp) continue;

        int lsb, adv;
        stbtt_GetGlyphHMetrics(&font, glyph, &adv, &lsb);
        int glyph_advance = (int)(adv * fscale);
        int x_offset = (rw - glyph_advance) / 2 + (int)(lsb * fscale);
        int y_offset = baseline + gy_off;

        for (int gy = 0; gy < gh; gy++) {
            for (int gx = 0; gx < gw; gx++) {
                int dx = gx + x_offset;
                int dy = gy + y_offset;
                if (dx >= 0 && dx < rw && dy >= 0 && dy < rh)
                    bitmaps[i][dy * rw + dx] = glyph_bmp[gy * gw + gx];
            }
        }
        stbtt_FreeBitmap(glyph_bmp, NULL);
    }
    return bitmaps;

fail:
    for (int i = 0; i < CHAR_COUNT; i++) free(bitmaps[i]);
    free(bitmaps);
    return NULL;
}

void char_db_free(CharDatabase *db) {
    for (int i = 0; i < CHAR_COUNT; i++) {
        free(db->entries[i].bitmap);
        db->entries[i].bitmap = NULL;
    }
    if (db->owns_font_data)
        free(db->font_data);
    db->font_data = NULL;
}
