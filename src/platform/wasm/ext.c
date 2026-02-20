/*
 * ext.c — Minimal WASM entry point for the Chrome extension.
 *
 * Reuses the Glif rendering pipeline and WebGL viewport shader from vp_render.h,
 * but strips out Nuklear, Clay, and all UI layout. The extension popup
 * drives parameters via ext_set_params(); the JS frame loop calls
 * ext_frame() with raw RGBA pixels captured from a <video> element.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include <GLES2/gl2.h>

#include "image.h"
#include "sampling.h"
#include "grid.h"
#include "font.h"
#include "contrast.h"
#include "match.h"
#include "temporal.h"

#include "geist_pixel_square.h"

/* ── Font atlas rendering (shared with ui.c and player.c) ── */
#include "vp_render.h"

/* ── Extension state ── */

static struct {
    /* Glif pipeline */
    GlifCharDatabase db;
    GlifSamplingConfig sc;
    GlifPrecomputedMasks pm;
    int cell_w, cell_h;
    float dir_crunch, global_crunch;
    int pm_stride;

    /* Current frame result */
    GlifGrid grid;
    int has_result;

    /* Viewport WebGL */
    VpRenderState vp;

    /* Font data */
    uint8_t *font_data;
    int font_len;
    int font_loaded;

    /* Canvas state */
    float dpr;
    int canvas_w, canvas_h;

    /* Temporal smoothing */
    GlifNormSmoother norm_sm;
    GlifShapeSmoother shape_sm;
    GlifContrastSmoother contrast_sm;
    GlifMatchSmoother match_sm;

    /* Adaptive contrast */
    GlifAdaptiveContrast adaptive;

    /* Resolution mode */
    int hi_res; /* 0 = ~120 cols, 1 = ~240 cols */

    int initialized;
} ext;

/* ── Helpers ── */

static void compute_cell_size(int img_w, int *out_cw, int *out_ch) {
    int target_cols = ext.hi_res ? 240 : 120;
    int cw = img_w / target_cols;
    if (cw < 2) cw = 2;
    *out_cw = cw;
    *out_ch = cw * 2;
}

/* Upload GlifGrid char/color data and render fullscreen */
static void vp_render(const GlifGrid *grid) {
    if (!ext.vp.program || !ext.vp.atlas_tex || !grid || grid->rows <= 0)
        return;

    int cols = grid->cols;
    int rows = grid->rows;
    int ncells = rows * cols;

    uint8_t *char_data = malloc((size_t)ncells);
    if (!char_data) return;
    for (int i = 0; i < ncells; i++)
        char_data[i] = (uint8_t)(grid->cells[i].ch - 32);

    uint8_t *color_data = calloc((size_t)ncells, 3);
    if (!color_data) { free(char_data); return; }
    for (int i = 0; i < ncells; i++) {
        color_data[i * 3 + 0] = grid->cells[i].r;
        color_data[i * 3 + 1] = grid->cells[i].g;
        color_data[i * 3 + 2] = grid->cells[i].b;
    }

    vp_render_raw(&ext.vp, char_data, color_data, cols, rows,
                  ext.canvas_w, ext.canvas_h);

    free(char_data);
    free(color_data);
}

/* ── Pipeline rebuild ── */

static void rebuild_pipeline(void) {
    if (!ext.font_loaded) return;

    glif_char_db_free(&ext.db);
    if (ext.pm_stride != 0) {
        glif_sampling_precompute_free(&ext.pm);
        ext.pm_stride = 0;
    }

    glif_sampling_config_init(&ext.sc);
    if (glif_char_db_create_from_memory(&ext.db, ext.font_data, (size_t)ext.font_len,
                                   ext.cell_w, ext.cell_h, &ext.sc) != 0) {
        ext.font_loaded = 0;
        return;
    }

    vp_build_font_atlas(&ext.vp, ext.font_data, ext.font_len,
                        ext.cell_w, ext.cell_h, ext.dpr);
}

static void process_frame(const uint8_t *pixels, int w, int h) {
    if (!ext.font_loaded) return;

    GlifImage img;
    if (glif_image_load_buffer(&img, pixels, w, h, 4) != 0) return;

    /* Recompute cell size if image width changed */
    int new_cw, new_ch;
    compute_cell_size(w, &new_cw, &new_ch);
    if (new_cw != ext.cell_w || new_ch != ext.cell_h) {
        ext.cell_w = new_cw;
        ext.cell_h = new_ch;
        if (ext.pm_stride != 0) {
            glif_sampling_precompute_free(&ext.pm);
            ext.pm_stride = 0;
        }
        /* Reset smoothers — grid dimensions changed */
        glif_norm_smoother_init(&ext.norm_sm);
        glif_shape_smoother_free(&ext.shape_sm);
        glif_contrast_smoother_init(&ext.contrast_sm);
        glif_match_smoother_free(&ext.match_sm);
        rebuild_pipeline();
    }

    /* Rebuild masks if stride changed */
    if (ext.pm_stride != w) {
        if (ext.pm_stride != 0) glif_sampling_precompute_free(&ext.pm);
        if (glif_sampling_precompute(&ext.pm, &ext.sc, ext.cell_w, ext.cell_h, w) != 0)
            return;
        ext.pm_stride = w;
    }

    GlifLightnessMap lm;
    if (glif_lightness_map_create(&lm, &img) != 0) return;

    if (ext.has_result) glif_grid_free(&ext.grid);

    if (glif_grid_create(&ext.grid, &img, ext.cell_w, ext.cell_h) != 0) {
        glif_lightness_map_free(&lm);
        return;
    }

    /* Always use temporal smoothing for video */
    glif_norm_smoother_apply(&ext.norm_sm, &lm, 0.4f);
    glif_grid_compute_vectors_fast(&ext.grid, &lm, &ext.pm);
    glif_shape_smoother_apply(&ext.shape_sm, &ext.grid, 0.4f);
    glif_grid_compute_colors(&ext.grid, &img);
    glif_contrast_analyze_frame(&ext.adaptive, &ext.grid);
    glif_contrast_smoother_apply(&ext.contrast_sm, &ext.adaptive, 0.3f);
    glif_contrast_directional(&ext.grid, &ext.sc, ext.dir_crunch, &ext.adaptive);
    glif_contrast_global(&ext.grid, ext.global_crunch, &ext.adaptive);
    glif_match_smoother_apply(&ext.match_sm, &ext.grid, &ext.db, 0.15f);

    ext.has_result = 1;
    glif_lightness_map_free(&lm);
}

/* ── Exported WASM API ── */

EMSCRIPTEN_KEEPALIVE
void ext_init(float dpr, int canvas_w, int canvas_h) {
    memset(&ext, 0, sizeof(ext));
    ext.dpr = dpr;
    ext.canvas_w = canvas_w;
    ext.canvas_h = canvas_h;
    ext.cell_w = 10;
    ext.cell_h = 20;
    ext.dir_crunch = 1.25f;
    ext.global_crunch = 1.5f;
    ext.adaptive.floor = 5.0f / 255.0f;
    ext.adaptive.ceil = 80.0f / 255.0f;

#ifdef __EMSCRIPTEN__
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha = 1; /* Transparent background for overlay */
    attrs.depth = 0;
    attrs.stencil = 0;
    attrs.antialias = 0;
    attrs.premultipliedAlpha = 1;
    attrs.majorVersion = 1;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx_handle =
        emscripten_webgl_create_context("#glif-ext-canvas", &attrs);
    emscripten_webgl_make_context_current(ctx_handle);
#endif

    /* Load embedded font */
    ext.font_data = malloc(geist_pixel_square_ttf_len);
    if (ext.font_data) {
        memcpy(ext.font_data, geist_pixel_square_ttf, geist_pixel_square_ttf_len);
        ext.font_len = (int)geist_pixel_square_ttf_len;
        ext.font_loaded = 1;
    }

    vp_state_init(&ext.vp);
    glif_sampling_config_init(&ext.sc);

    if (ext.font_loaded)
        rebuild_pipeline();

    ext.initialized = 1;
}

EMSCRIPTEN_KEEPALIVE
void ext_resize(int canvas_w, int canvas_h) {
    ext.canvas_w = canvas_w;
    ext.canvas_h = canvas_h;
}

EMSCRIPTEN_KEEPALIVE
void ext_frame(const uint8_t *pixels, int w, int h) {
    if (!ext.initialized) return;

    process_frame(pixels, w, h);

    if (ext.has_result) {
        glViewport(0, 0, ext.canvas_w, ext.canvas_h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        vp_render(&ext.grid);
    }
}

EMSCRIPTEN_KEEPALIVE
void ext_render(void) {
    if (!ext.initialized || !ext.has_result) return;

    glViewport(0, 0, ext.canvas_w, ext.canvas_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    vp_render(&ext.grid);
}

EMSCRIPTEN_KEEPALIVE
void ext_set_params(float dir_crunch, float global_crunch) {
    ext.dir_crunch = dir_crunch;
    ext.global_crunch = global_crunch;
}

EMSCRIPTEN_KEEPALIVE
void ext_set_hires(int hires) {
    if (hires == ext.hi_res) return;
    ext.hi_res = hires;

    /* Force cell size recomputation on next frame by invalidating stride */
    if (ext.pm_stride != 0) {
        glif_sampling_precompute_free(&ext.pm);
        ext.pm_stride = 0;
    }
    ext.cell_w = 10;
    ext.cell_h = 20;

    glif_norm_smoother_init(&ext.norm_sm);
    glif_shape_smoother_free(&ext.shape_sm);
    glif_contrast_smoother_init(&ext.contrast_sm);
    glif_match_smoother_free(&ext.match_sm);

    if (ext.has_result) {
        glif_grid_free(&ext.grid);
        ext.has_result = 0;
    }
}
