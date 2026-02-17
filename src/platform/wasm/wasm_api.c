#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include "image.h"
#include "sampling.h"
#include "grid.h"
#include "font.h"
#include "contrast.h"
#include "match.h"

typedef struct {
    CharDatabase db;
    SamplingConfig sc;
    PrecomputedMasks pm;
    int cell_w;
    int cell_h;
    float dir_crunch;
    float global_crunch;
    int pm_stride;  /* stride used to build current masks */
} GlifContext;

EMSCRIPTEN_KEEPALIVE
GlifContext *glif_init(const uint8_t *font_data, int font_len,
                       int cell_w, int cell_h,
                       float dir_crunch, float global_crunch) {
    GlifContext *ctx = calloc(1, sizeof(GlifContext));
    if (!ctx) return NULL;

    ctx->cell_w = cell_w;
    ctx->cell_h = cell_h;
    ctx->dir_crunch = dir_crunch;
    ctx->global_crunch = global_crunch;
    ctx->pm_stride = 0;

    sampling_config_init(&ctx->sc);

    if (char_db_create_from_memory(&ctx->db, font_data, (size_t)font_len,
                                   cell_w, cell_h, &ctx->sc) != 0) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

EMSCRIPTEN_KEEPALIVE
int glif_process_frame(GlifContext *ctx, const uint8_t *pixels,
                       int w, int h, int channels,
                       char *out_chars, uint8_t *out_r,
                       uint8_t *out_g, uint8_t *out_b) {
    Image img;
    if (image_load_buffer(&img, pixels, w, h, channels) != 0)
        return 0;

    /* Rebuild precomputed masks if stride changed */
    if (ctx->pm_stride != w) {
        if (ctx->pm_stride != 0)
            sampling_precompute_free(&ctx->pm);
        if (sampling_precompute(&ctx->pm, &ctx->sc, ctx->cell_w, ctx->cell_h, w) != 0)
            return 0;
        ctx->pm_stride = w;
    }

    LightnessMap lm;
    if (lightness_map_create(&lm, &img) != 0)
        return 0;

    Grid grid;
    if (grid_create(&grid, &img, ctx->cell_w, ctx->cell_h) != 0) {
        lightness_map_free(&lm);
        return 0;
    }

    grid_compute_vectors_fast(&grid, &lm, &ctx->pm);
    grid_compute_colors(&grid, &img);
    contrast_directional(&grid, &ctx->sc, ctx->dir_crunch, NULL);
    contrast_global(&grid, ctx->global_crunch, NULL);
    match_grid(&grid, &ctx->db);

    int ncells = grid.rows * grid.cols;
    for (int i = 0; i < ncells; i++) {
        out_chars[i] = grid.cells[i].ch;
        out_r[i] = grid.cells[i].r;
        out_g[i] = grid.cells[i].g;
        out_b[i] = grid.cells[i].b;
    }

    int result = grid.rows | (grid.cols << 16);

    grid_free(&grid);
    lightness_map_free(&lm);

    return result;
}

EMSCRIPTEN_KEEPALIVE
void glif_free(GlifContext *ctx) {
    if (!ctx) return;
    char_db_free(&ctx->db);
    if (ctx->pm_stride != 0)
        sampling_precompute_free(&ctx->pm);
    free(ctx);
}
