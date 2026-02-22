#include <stdlib.h>
#include <string.h>
#include <stdio.h>
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

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_STANDARD_IO
#include "nuklear.h"
#include "clay.h"

#include "nk_webgl.h"
#include "ui_layout.h"
#include "geist_pixel_square.h"

/* ── Font atlas rendering (shared with ext.c and player.c) ── */
#include "vp_render.h"

/* ── Application state ── */

static struct {
    /* Nuklear */
    struct nk_context nk;
    struct nk_font_atlas nk_atlas;
    struct nk_font *nk_font;
    NkWebGL nk_gl;

    /* Clay */
    Clay_Context *clay_ctx;
    void *clay_mem;

    /* Glif pipeline */
    GlifCharDatabase db;
    GlifSamplingConfig sc;
    GlifPrecomputedMasks pm;
    int cell_w, cell_h;
    float dir_crunch, global_crunch;
    GlifAdaptiveContrast adaptive;
    int adaptive_on;
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
    int canvas_w, canvas_h; /* physical pixels */

    /* Last rendered content rect (CSS logical pixels, relative to canvas) */
    float content_x, content_y, content_w, content_h;

    /* Input state */
    int mouse_x, mouse_y, mouse_buttons;
    int initialized;

    /* Pending image/video frame */
    uint8_t *pending_pixels;
    int pending_w, pending_h, pending_channels;
    int pending_dirty;

    /* Resolution mode */
    int hi_res; /* 0 = ~120x40, 1 = ~240x80 (4x chars) */

    /* Temporal smoothing */
    int stabilize;
    GlifNormSmoother norm_sm;
    GlifShapeSmoother shape_sm;
    GlifContrastSmoother contrast_sm;
    GlifMatchSmoother match_sm;

    /* Previous parameter values for change detection */
    float prev_dir_crunch, prev_global_crunch;
    int prev_adaptive_on;
    int prev_hi_res;
    int prev_stabilize;

    /* Media bar state */
    int has_content;         /* Content loaded (image or video) */
    int video_mode;          /* 1 = video, 0 = image */
    int playing;             /* Video is currently playing */
    float current_time;      /* Current playback position (seconds) */
    float duration;          /* Total duration (seconds) */
    int audio_available;     /* Video has an audio track */
    int audio_muted;         /* Audio is muted */
    int seeking;             /* User is dragging progress slider */
    float hdr_intensity;     /* 0.0 = off, 1.0 = full HDR */
    int export_format;       /* 0=PNG, 1=PPM, 2=SVG */
    int speed_idx;           /* 0-4, maps to 0.25/0.5/1/2/4x */
    int compare_on;          /* Compare toggle state */
    int compare_dragging;    /* Currently dragging compare split */
    int prev_mouse_buttons;  /* Previous frame's mouse buttons */
} app;

/* Compute cell dimensions from image size and resolution mode.
 * Lo targets ~120 columns, Hi targets ~240 columns (4x char count). */
static void compute_cell_size(int img_w, int img_h, int hi_res,
                              int *out_cw, int *out_ch) {
    (void)img_h;
    int target_cols = hi_res ? 240 : 120;
    int cw = img_w / target_cols;
    if (cw < 2) cw = 2;
    int ch = cw * 2;
    *out_cw = cw;
    *out_ch = ch;
}

/* Speed table: index → playback rate */
static const float speed_table[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
#define SPEED_COUNT 5

/* Format time as "M:SS" into buf */
static void format_time(char *buf, int len, float seconds) {
    int s = (int)seconds;
    if (s < 0) s = 0;
    int m = s / 60;
    int ss = s % 60;
    snprintf(buf, (size_t)len, "%d:%02d", m, ss);
}

/* Styled toggle button: push accent color when active */
static int toggle_button(struct nk_context *nk, const char *label, int active) {
    if (active) {
        nk_style_push_color(nk, &nk->style.button.text_normal,
                            nk_rgb(74, 158, 255));
        nk_style_push_color(nk, &nk->style.button.text_hover,
                            nk_rgb(74, 158, 255));
        nk_style_push_color(nk, &nk->style.button.text_active,
                            nk_rgb(74, 158, 255));
    }
    int clicked = nk_button_label(nk, label);
    if (active) {
        nk_style_pop_color(nk);
        nk_style_pop_color(nk);
        nk_style_pop_color(nk);
    }
    return clicked;
}

/* Styled toggle button using a symbol icon */
static int toggle_button_symbol(struct nk_context *nk, enum nk_symbol_type sym, int active) {
    if (active) {
        nk_style_push_color(nk, &nk->style.button.text_normal,
                            nk_rgb(74, 158, 255));
        nk_style_push_color(nk, &nk->style.button.text_hover,
                            nk_rgb(74, 158, 255));
        nk_style_push_color(nk, &nk->style.button.text_active,
                            nk_rgb(74, 158, 255));
    }
    int clicked = nk_button_symbol(nk, sym);
    if (active) {
        nk_style_pop_color(nk);
        nk_style_pop_color(nk);
        nk_style_pop_color(nk);
    }
    return clicked;
}

/* Upload GlifGrid char/color data and render within Clay bounds */
static void vp_render(const GlifGrid *grid, Clay_BoundingBox bounds) {
    if (!app.vp.program || !app.vp.atlas_tex || !grid || grid->rows <= 0)
        return;

    int cols = grid->cols;
    int rows = grid->rows;
    int ncells = rows * cols;

    /* Upload char grid */
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

    vp_upload(&app.vp, char_data, color_data, cols, rows);
    free(char_data);
    free(color_data);

    /* Viewport scissor (physical pixels) */
    int vp_x = (int)(bounds.x * app.dpr);
    int vp_y = (int)((float)app.canvas_h - (bounds.y + bounds.height) * app.dpr);
    int vp_w = (int)(bounds.width * app.dpr);
    int vp_h = (int)(bounds.height * app.dpr);

    glEnable(GL_SCISSOR_TEST);
    glScissor(vp_x, vp_y, vp_w, vp_h);
    glViewport(vp_x, vp_y, vp_w, vp_h);

    vp_draw(&app.vp, cols, rows, vp_w, vp_h, app.hdr_intensity);

    /* Store content rect in CSS logical pixels for compare divider/drag */
    float grid_px_w = (float)cols * (float)app.vp.atlas_cell_w;
    float grid_px_h = (float)rows * (float)app.vp.atlas_cell_h;
    float sx = (float)vp_w / grid_px_w;
    float sy = (float)vp_h / grid_px_h;
    float s = (sx < sy) ? sx : sy;
    float rw = grid_px_w * s;
    float rh = grid_px_h * s;
    app.content_x = bounds.x + ((float)vp_w - rw) * 0.5f / app.dpr;
    app.content_y = bounds.y + ((float)vp_h - rh) * 0.5f / app.dpr;
    app.content_w = rw / app.dpr;
    app.content_h = rh / app.dpr;

    glDisable(GL_SCISSOR_TEST);
}

/* ── Pipeline rebuild ── */

static void rebuild_pipeline(void) {
    if (!app.font_loaded) return;

    glif_char_db_free(&app.db);
    if (app.pm_stride != 0) {
        glif_sampling_precompute_free(&app.pm);
        app.pm_stride = 0;
    }

    glif_sampling_config_init(&app.sc);
    if (glif_char_db_create_from_memory(&app.db, app.font_data, (size_t)app.font_len,
                                   app.cell_w, app.cell_h, &app.sc) != 0) {
        app.font_loaded = 0;
        return;
    }

    vp_build_font_atlas(&app.vp, app.font_data, app.font_len,
                        app.cell_w, app.cell_h, app.dpr);
}

static void process_frame(const uint8_t *pixels, int w, int h, int channels) {
    if (!app.font_loaded) return;

    GlifImage img;
    if (glif_image_load_buffer(&img, pixels, w, h, channels) != 0) return;

    /* Rebuild masks if stride changed */
    if (app.pm_stride != w) {
        if (app.pm_stride != 0) glif_sampling_precompute_free(&app.pm);
        if (glif_sampling_precompute(&app.pm, &app.sc, app.cell_w, app.cell_h, w) != 0)
            return;
        app.pm_stride = w;
    }

    GlifLightnessMap lm;
    if (glif_lightness_map_create(&lm, &img) != 0) return;

    if (app.has_result) glif_grid_free(&app.grid);

    if (glif_grid_create(&app.grid, &img, app.cell_w, app.cell_h) != 0) {
        glif_lightness_map_free(&lm);
        return;
    }

    if (app.stabilize)
        glif_norm_smoother_apply(&app.norm_sm, &lm, 0.4f);
    else
        glif_lightness_map_normalize(&lm);

    /* Temporarily disable adaptive per-cell crunch when toggle is off */
    float saved_floor = app.adaptive.floor;
    if (!app.adaptive_on)
        app.adaptive.floor = -1.0f;

    glif_grid_compute_vectors_fast(&app.grid, &lm, &app.pm);

    if (app.stabilize)
        glif_shape_smoother_apply(&app.shape_sm, &app.grid, 0.4f);

    glif_grid_compute_colors(&app.grid, &img);
    glif_contrast_analyze_frame(&app.adaptive, &app.grid);

    if (app.stabilize)
        glif_contrast_smoother_apply(&app.contrast_sm, &app.adaptive, 0.3f);

    glif_contrast_directional(&app.grid, &app.sc, app.dir_crunch, &app.adaptive);
    glif_contrast_global(&app.grid, app.global_crunch, &app.adaptive);

    app.adaptive.floor = saved_floor;

    if (app.stabilize)
        glif_match_smoother_apply(&app.match_sm, &app.grid, &app.db, 0.15f);
    else
        glif_match_grid(&app.grid, &app.db);

    app.has_result = 1;
    glif_lightness_map_free(&lm);
}

/* ── Exported WASM API ── */

EMSCRIPTEN_KEEPALIVE
void app_init(float dpr, int canvas_w, int canvas_h) {
    memset(&app, 0, sizeof(app));
    app.dpr = dpr;
    app.canvas_w = canvas_w;
    app.canvas_h = canvas_h;
    app.cell_w = 10;
    app.cell_h = 20;
    app.dir_crunch = 1.25f;
    app.global_crunch = 1.5f;
    app.adaptive_on = 1;
    app.stabilize = 1;
    app.adaptive.floor = 5.0f / 255.0f;
    app.adaptive.ceil = 80.0f / 255.0f;
    app.speed_idx = 2; /* 1x */
    app.audio_muted = 1;

#ifdef __EMSCRIPTEN__
    /* Create Emscripten WebGL context on the canvas */
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha = 0;
    attrs.depth = 0;
    attrs.stencil = 0;
    attrs.antialias = 0;
    attrs.majorVersion = 1; /* WebGL 1 = GLES 2 */
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx_handle =
        emscripten_webgl_create_context("#canvas", &attrs);
    emscripten_webgl_make_context_current(ctx_handle);
#endif

    /* Use embedded Geist Pixel Square font for both Nuklear UI and viewport */
    app.font_data = malloc(geist_pixel_square_ttf_len);
    if (app.font_data) {
        memcpy(app.font_data, geist_pixel_square_ttf, geist_pixel_square_ttf_len);
        app.font_len = (int)geist_pixel_square_ttf_len;
        app.font_loaded = 1;
    }

    /* Init Nuklear + WebGL backend with Geist font */
    nk_webgl_init(&app.nk_gl, dpr);
    nk_webgl_font_stash_begin(&app.nk_gl, &app.nk_atlas);
    app.nk_font = nk_font_atlas_add_from_memory(&app.nk_atlas,
        app.font_data, (nk_size)app.font_len, 14.0f * dpr, NULL);
    nk_webgl_font_stash_end(&app.nk_gl, &app.nk_atlas);
    app.nk_font->handle.height = 14.0f; /* logical size for layout; atlas is baked at 14*dpr */
    nk_init_default(&app.nk, &app.nk_font->handle);

    /* Init Clay */
    uint32_t clay_mem_size = Clay_MinMemorySize();
    app.clay_mem = malloc(clay_mem_size);
    if (!app.clay_mem) return;
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(clay_mem_size, app.clay_mem);
    Clay_ErrorHandler err = { 0 };
    app.clay_ctx = Clay_Initialize(arena,
        (Clay_Dimensions){ (float)canvas_w / dpr, (float)canvas_h / dpr }, err);
    Clay_SetMeasureTextFunction(ui_measure_text, NULL);

    /* Init viewport shader */
    vp_state_init(&app.vp);

    glif_sampling_config_init(&app.sc);

    /* Build pipeline with default font */
    if (app.font_loaded)
        rebuild_pipeline();

    /* Init change-detection state */
    app.prev_dir_crunch = app.dir_crunch;
    app.prev_global_crunch = app.global_crunch;
    app.prev_adaptive_on = app.adaptive_on;
    app.prev_hi_res = app.hi_res;
    app.prev_stabilize = app.stabilize;

    app.initialized = 1;
}

EMSCRIPTEN_KEEPALIVE
void app_resize(int canvas_w, int canvas_h) {
    app.canvas_w = canvas_w;
    app.canvas_h = canvas_h;
    if (app.initialized && app.dpr > 0.0f) {
        Clay_SetLayoutDimensions((Clay_Dimensions){
            (float)canvas_w / app.dpr, (float)canvas_h / app.dpr });
    }
}

EMSCRIPTEN_KEEPALIVE
void app_set_dpr(float dpr) {
    if (dpr <= 0.0f || dpr == app.dpr) return;
    app.dpr = dpr;
    app.nk_gl.dpr = dpr;

    if (!app.font_data || app.font_len <= 0) return;

    /* Delete old Nuklear font texture and re-bake at new DPR */
    if (app.nk_gl.font_tex) {
        glDeleteTextures(1, &app.nk_gl.font_tex);
        app.nk_gl.font_tex = 0;
    }
    nk_font_atlas_clear(&app.nk_atlas);
    nk_webgl_font_stash_begin(&app.nk_gl, &app.nk_atlas);
    app.nk_font = nk_font_atlas_add_from_memory(&app.nk_atlas,
        app.font_data, (nk_size)app.font_len, 14.0f * dpr, NULL);
    nk_webgl_font_stash_end(&app.nk_gl, &app.nk_atlas);
    app.nk_font->handle.height = 14.0f; /* logical size for layout; atlas is baked at 14*dpr */
    nk_style_set_font(&app.nk, &app.nk_font->handle);

    /* Rebuild viewport glyph atlas at new physical cell size */
    vp_build_font_atlas(&app.vp, app.font_data, app.font_len,
                        app.cell_w, app.cell_h, app.dpr);
}

/* Toggle compare mode and notify JS for image upload / cursor */
static void toggle_compare_mode(void) {
    if (!app.has_content) return;
    app.compare_on = !app.compare_on;
    app.vp.compare_mode = app.compare_on;
    if (!app.compare_on) app.compare_dragging = 0;
#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (window.glifSetCursor) window.glifSetCursor($0 ? 'ew-resize' : null);
        if (window.glifOnCompareToggle) window.glifOnCompareToggle($0);
    }, app.compare_on);
#endif
}

EMSCRIPTEN_KEEPALIVE
void app_frame(void) {
    if (!app.initialized) return;
    if (app.canvas_w <= 0 || app.canvas_h <= 0 || app.dpr <= 0.0f) return;

    int logical_w = (int)((float)app.canvas_w / app.dpr);
    int logical_h = (int)((float)app.canvas_h / app.dpr);
    if (logical_w <= 0 || logical_h <= 0) return;

    int mouse_down = (app.mouse_buttons & 1) != 0;
    int prev_down = (app.prev_mouse_buttons & 1) != 0;

    /* Nuklear input (suppress during compare drag) */
    nk_input_begin(&app.nk);
    if (!app.compare_dragging) {
        nk_input_motion(&app.nk, app.mouse_x, app.mouse_y);
        nk_input_button(&app.nk, NK_BUTTON_LEFT,
                        app.mouse_x, app.mouse_y, mouse_down);
    }
    nk_input_end(&app.nk);

    /* Clay layout */
    Clay_SetPointerState((Clay_Vector2){ (float)app.mouse_x, (float)app.mouse_y },
                         (app.mouse_buttons & 1) != 0);
    Clay_BeginLayout();
    UiLayout layout;
    ui_layout_build(&layout, logical_w, logical_h,
                    app.has_content, app.video_mode);
    Clay_RenderCommandArray commands = Clay_EndLayout();
    /* Get bounds AFTER EndLayout computes positions */
    ui_layout_get_bounds(&layout);

    /* Recompute content rect from current layout (before drag handler) */
    if (app.has_result && layout.viewport.width > 0 &&
        app.vp.atlas_cell_w > 0) {
        int vp_w = (int)(layout.viewport.width * app.dpr);
        int vp_h = (int)(layout.viewport.height * app.dpr);
        float gpw = (float)app.grid.cols * (float)app.vp.atlas_cell_w;
        float gph = (float)app.grid.rows * (float)app.vp.atlas_cell_h;
        float sx = (float)vp_w / gpw;
        float sy = (float)vp_h / gph;
        float s = (sx < sy) ? sx : sy;
        float rw = gpw * s, rh = gph * s;
        app.content_x = layout.viewport.x +
                         ((float)vp_w - rw) * 0.5f / app.dpr;
        app.content_y = layout.viewport.y +
                         ((float)vp_h - rh) * 0.5f / app.dpr;
        app.content_w = rw / app.dpr;
        app.content_h = rh / app.dpr;
    }

    /* Compare drag handling (uses current frame's content rect) */
    if (app.compare_on && app.has_content && app.content_w > 0) {
        if (mouse_down && !prev_down && !app.compare_dragging) {
            float mx = (float)app.mouse_x;
            float my = (float)app.mouse_y;
            if (mx >= app.content_x && mx <= app.content_x + app.content_w &&
                my >= app.content_y && my <= app.content_y + app.content_h) {
                app.compare_dragging = 1;
            }
        }
        if (app.compare_dragging && mouse_down) {
            float rel = ((float)app.mouse_x - app.content_x) / app.content_w;
            if (rel < 0.0f) rel = 0.0f;
            if (rel > 1.0f) rel = 1.0f;
            app.vp.split_pos = rel;
        }
    }
    if (!mouse_down) app.compare_dragging = 0;
    app.prev_mouse_buttons = app.mouse_buttons;

    /* Drop overlay: click anywhere in viewport opens file picker */
#ifdef __EMSCRIPTEN__
    if (!app.has_content && mouse_down && !prev_down) {
        float mx = (float)app.mouse_x;
        float my = (float)app.mouse_y;
        if (mx >= layout.viewport.x &&
            mx < layout.viewport.x + layout.viewport.width &&
            my >= layout.viewport.y &&
            my < layout.viewport.y + layout.viewport.height) {
            EM_ASM({ if (window.glifPickFile) window.glifPickFile(); });
        }
    }
#endif

    /* Clear framebuffer */
    glViewport(0, 0, app.canvas_w, app.canvas_h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Draw Clay rectangles (backgrounds) via glScissor + glClear */
    for (int32_t i = 0; i < commands.length; i++) {
        Clay_RenderCommand *cmd = &commands.internalArray[i];
        if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
            Clay_BoundingBox b = cmd->boundingBox;
            Clay_RectangleRenderData r = cmd->renderData.rectangle;
            glEnable(GL_SCISSOR_TEST);
            glScissor(
                (GLint)(b.x * app.dpr),
                (GLint)((float)app.canvas_h - (b.y + b.height) * app.dpr),
                (GLsizei)(b.width * app.dpr),
                (GLsizei)(b.height * app.dpr));
            glClearColor(r.backgroundColor.r / 255.0f,
                         r.backgroundColor.g / 255.0f,
                         r.backgroundColor.b / 255.0f,
                         r.backgroundColor.a / 255.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);
        }
    }

    /* Nuklear UI widgets in toolbar region */
    if (layout.toolbar.width > 0 && layout.toolbar.height > 0) {
        struct nk_rect tb = nk_rect(layout.toolbar.x, layout.toolbar.y,
                                    layout.toolbar.width, layout.toolbar.height);
        /* Force Nuklear window to match Clay-computed bounds every frame.
           nk_begin only uses bounds for initial creation; this overrides stored position. */
        nk_window_set_bounds(&app.nk, "Toolbar", tb);

        /* Add inner padding and item spacing */
        nk_style_push_vec2(&app.nk, &app.nk.style.window.padding,
                           nk_vec2(12, 8));
        nk_style_push_vec2(&app.nk, &app.nk.style.window.spacing,
                           nk_vec2(8, 6));

        if (nk_begin(&app.nk, "Toolbar", tb, NK_WINDOW_NO_SCROLLBAR)) {
            if (layout.is_mobile) {
                /* Mobile: stacked rows */
                nk_layout_row_dynamic(&app.nk, 24, 2);
                nk_property_float(&app.nk, "Edge", 0.0f, &app.dir_crunch, 10.0f, 0.05f, 0.05f);
                nk_property_float(&app.nk, "Contrast", 0.0f, &app.global_crunch, 10.0f, 0.05f, 0.05f);

                nk_layout_row_dynamic(&app.nk, 28, 4);
                nk_checkbox_label(&app.nk, "Adaptive", &app.adaptive_on);
                nk_checkbox_label(&app.nk, "Stable", &app.stabilize);
                if (nk_button_label(&app.nk, app.hi_res ? "Hi Res" : "Lo Res"))
                    app.hi_res = !app.hi_res;
#ifdef __EMSCRIPTEN__
                if (nk_button_label(&app.nk, "Camera"))
                    EM_ASM({ if (window.glifToggleCamera) window.glifToggleCamera(); });
#endif
            } else if (layout.toolbar_wrap) {
                /* Medium width: 2 rows */
                nk_layout_row_dynamic(&app.nk, 24, 2);
                nk_property_float(&app.nk, "Edge", 0.0f, &app.dir_crunch, 10.0f, 0.05f, 0.05f);
                nk_property_float(&app.nk, "Contrast", 0.0f, &app.global_crunch, 10.0f, 0.05f, 0.05f);

                nk_layout_row_dynamic(&app.nk, 24, 4);
                nk_checkbox_label(&app.nk, "Adaptive", &app.adaptive_on);
                nk_checkbox_label(&app.nk, "Stable", &app.stabilize);
                if (nk_button_label(&app.nk, app.hi_res ? "Hi Res" : "Lo Res"))
                    app.hi_res = !app.hi_res;
#ifdef __EMSCRIPTEN__
                if (nk_button_label(&app.nk, "Webcam"))
                    EM_ASM({ if (window.glifToggleCamera) window.glifToggleCamera(); });
#else
                nk_button_label(&app.nk, "Webcam");
#endif
            } else {
                /* Wide desktop: single row */
                nk_layout_row_dynamic(&app.nk, 28, 6);
                nk_property_float(&app.nk, "Edge", 0.0f, &app.dir_crunch, 10.0f, 0.05f, 0.05f);
                nk_property_float(&app.nk, "Contrast", 0.0f, &app.global_crunch, 10.0f, 0.05f, 0.05f);
                nk_checkbox_label(&app.nk, "Adaptive", &app.adaptive_on);
                nk_checkbox_label(&app.nk, "Stable", &app.stabilize);
                if (nk_button_label(&app.nk, app.hi_res ? "Hi Res" : "Lo Res"))
                    app.hi_res = !app.hi_res;

#ifdef __EMSCRIPTEN__
                if (nk_button_label(&app.nk, "Webcam"))
                    EM_ASM({ if (window.glifToggleCamera) window.glifToggleCamera(); });
#else
                nk_button_label(&app.nk, "Webcam");
#endif
            }
        }
        nk_end(&app.nk);

        nk_style_pop_vec2(&app.nk);
        nk_style_pop_vec2(&app.nk);
    }

    /* Nuklear media bar */
    if (app.has_content && layout.media_bar.width > 0 && layout.media_bar.height > 0) {
        struct nk_rect mb = nk_rect(layout.media_bar.x, layout.media_bar.y,
                                    layout.media_bar.width, layout.media_bar.height);
        nk_window_set_bounds(&app.nk, "MediaBar", mb);

        nk_style_push_vec2(&app.nk, &app.nk.style.window.padding,
                           nk_vec2(8, 6));
        nk_style_push_vec2(&app.nk, &app.nk.style.window.spacing,
                           nk_vec2(4, 0));

        if (nk_begin(&app.nk, "MediaBar", mb, NK_WINDOW_NO_SCROLLBAR)) {
            if (app.video_mode) {
                /* Video mode: Play | Time | Progress | Speed | Audio | HDR | Cmp | Fmt | Exp | Full */
                nk_layout_row_template_begin(&app.nk, 28);
                nk_layout_row_template_push_static(&app.nk, 36);   /* Play */
                nk_layout_row_template_push_static(&app.nk, 80);   /* Time */
                nk_layout_row_template_push_dynamic(&app.nk);       /* Progress */
                nk_layout_row_template_push_static(&app.nk, 40);   /* Speed */
                if (app.audio_available)
                    nk_layout_row_template_push_static(&app.nk, 36); /* Audio */
                nk_layout_row_template_push_static(&app.nk, 36);   /* HDR */
                nk_layout_row_template_push_static(&app.nk, 36);   /* Cmp */
                nk_layout_row_template_push_static(&app.nk, 40);   /* Fmt */
                nk_layout_row_template_push_static(&app.nk, 36);   /* Exp */
                nk_layout_row_template_push_static(&app.nk, 36);   /* Full */
                nk_layout_row_template_end(&app.nk);

                /* Play/Pause */
#ifdef __EMSCRIPTEN__
                if (nk_button_symbol(&app.nk, app.playing
                        ? NK_SYMBOL_RECT_SOLID : NK_SYMBOL_TRIANGLE_RIGHT))
                    EM_ASM({ if (window.glifPlayPause) window.glifPlayPause(); });
#else
                nk_button_symbol(&app.nk, app.playing
                    ? NK_SYMBOL_RECT_SOLID : NK_SYMBOL_TRIANGLE_RIGHT);
#endif

                /* Time display */
                char time_buf[32];
                char cur_buf[12], dur_buf[12];
                format_time(cur_buf, sizeof(cur_buf), app.current_time);
                format_time(dur_buf, sizeof(dur_buf), app.duration);
                snprintf(time_buf, sizeof(time_buf), "%s/%s", cur_buf, dur_buf);
                nk_label(&app.nk, time_buf, NK_TEXT_CENTERED);

                /* Progress slider */
                float progress = app.duration > 0.0f
                    ? app.current_time / app.duration : 0.0f;
                float old_progress = progress;
                nk_slider_float(&app.nk, 0.0f, &progress, 1.0f, 0.001f);
                if (progress != old_progress) {
                    app.seeking = 1;
#ifdef __EMSCRIPTEN__
                    float seek_time = progress * app.duration;
                    EM_ASM({ if (window.glifSeek) window.glifSeek($0); },
                           (double)seek_time);
#endif
                } else if (app.seeking && !(app.mouse_buttons & 1)) {
                    app.seeking = 0;
                }

                /* Speed */
                {
                    char spd[8];
                    if (speed_table[app.speed_idx] < 1.0f)
                        snprintf(spd, sizeof(spd), "%.2gx",
                                 (double)speed_table[app.speed_idx]);
                    else
                        snprintf(spd, sizeof(spd), "%.0fx",
                                 (double)speed_table[app.speed_idx]);
                    if (nk_button_label(&app.nk, spd)) {
                        app.speed_idx = (app.speed_idx + 1) % SPEED_COUNT;
#ifdef __EMSCRIPTEN__
                        EM_ASM({ if (window.glifSetSpeed) window.glifSetSpeed($0); },
                               (double)speed_table[app.speed_idx]);
#endif
                    }
                }

                /* Audio (conditional) */
                if (app.audio_available) {
#ifdef __EMSCRIPTEN__
                    if (app.audio_muted) {
                        if (nk_button_symbol(&app.nk, NK_SYMBOL_X))
                            EM_ASM({ if (window.glifToggleAudio) window.glifToggleAudio(); });
                    } else {
                        if (toggle_button_symbol(&app.nk, NK_SYMBOL_CIRCLE_SOLID, 1))
                            EM_ASM({ if (window.glifToggleAudio) window.glifToggleAudio(); });
                    }
#else
                    if (app.audio_muted)
                        nk_button_symbol(&app.nk, NK_SYMBOL_X);
                    else
                        toggle_button_symbol(&app.nk, NK_SYMBOL_CIRCLE_SOLID, 1);
#endif
                }

                /* HDR */
                if (toggle_button(&app.nk, "HDR", app.hdr_intensity > 0.0f))
                    app.hdr_intensity = app.hdr_intensity > 0.0f ? 0.0f : 1.0f;

                /* Compare */
                if (toggle_button(&app.nk, "Cmp", app.compare_on))
                    toggle_compare_mode();

                /* Format cycle */
                {
                    const char *fmt_labels[] = { "PNG", "PPM", "SVG" };
                    if (nk_button_label(&app.nk, fmt_labels[app.export_format]))
                        app.export_format = (app.export_format + 1) % 3;
                }

                /* Export */
#ifdef __EMSCRIPTEN__
                if (nk_button_symbol(&app.nk, NK_SYMBOL_TRIANGLE_DOWN))
                    EM_ASM({ if (window.glifExport) window.glifExport($0); },
                           app.export_format);
#else
                nk_button_symbol(&app.nk, NK_SYMBOL_TRIANGLE_DOWN);
#endif

                /* Fullscreen */
#ifdef __EMSCRIPTEN__
                if (nk_button_symbol(&app.nk, NK_SYMBOL_RECT_OUTLINE))
                    EM_ASM({ if (window.glifToggleFullscreen) window.glifToggleFullscreen(); });
#else
                nk_button_symbol(&app.nk, NK_SYMBOL_RECT_OUTLINE);
#endif
            } else {
                /* Image mode: spacer | HDR | Cmp | Fmt | Exp | Full */
                nk_layout_row_template_begin(&app.nk, 28);
                nk_layout_row_template_push_dynamic(&app.nk);       /* spacer */
                nk_layout_row_template_push_static(&app.nk, 40);   /* HDR */
                nk_layout_row_template_push_static(&app.nk, 40);   /* Cmp */
                nk_layout_row_template_push_static(&app.nk, 40);   /* Fmt */
                nk_layout_row_template_push_static(&app.nk, 40);   /* Exp */
                nk_layout_row_template_push_static(&app.nk, 40);   /* Full */
                nk_layout_row_template_end(&app.nk);

                /* Spacer */
                nk_label(&app.nk, "", NK_TEXT_LEFT);

                /* HDR */
                if (toggle_button(&app.nk, "HDR", app.hdr_intensity > 0.0f))
                    app.hdr_intensity = app.hdr_intensity > 0.0f ? 0.0f : 1.0f;

                /* Compare */
                if (toggle_button(&app.nk, "Cmp", app.compare_on))
                    toggle_compare_mode();

                /* Format cycle */
                {
                    const char *fmt_labels[] = { "PNG", "PPM", "SVG" };
                    if (nk_button_label(&app.nk, fmt_labels[app.export_format]))
                        app.export_format = (app.export_format + 1) % 3;
                }

                /* Export */
#ifdef __EMSCRIPTEN__
                if (nk_button_symbol(&app.nk, NK_SYMBOL_TRIANGLE_DOWN))
                    EM_ASM({ if (window.glifExport) window.glifExport($0); },
                           app.export_format);
#else
                nk_button_symbol(&app.nk, NK_SYMBOL_TRIANGLE_DOWN);
#endif

                /* Fullscreen */
#ifdef __EMSCRIPTEN__
                if (nk_button_symbol(&app.nk, NK_SYMBOL_RECT_OUTLINE))
                    EM_ASM({ if (window.glifToggleFullscreen) window.glifToggleFullscreen(); });
#else
                nk_button_symbol(&app.nk, NK_SYMBOL_RECT_OUTLINE);
#endif
            }
        }
        nk_end(&app.nk);

        nk_style_pop_vec2(&app.nk);
        nk_style_pop_vec2(&app.nk);
    }

    /* Drop overlay (no content loaded) */
    if (!app.has_content && layout.viewport.width > 0 && layout.viewport.height > 0) {
        float ow = 420, oh = 54;
        float ox = layout.viewport.x + (layout.viewport.width - ow) * 0.5f;
        float oy = layout.viewport.y + (layout.viewport.height - oh) * 0.5f;
        struct nk_rect drop_rect = nk_rect(ox, oy, ow, oh);
        nk_window_set_bounds(&app.nk, "Drop", drop_rect);

        nk_style_push_style_item(&app.nk, &app.nk.style.window.fixed_background,
            nk_style_item_color(nk_rgba(0, 0, 0, 0)));
        nk_style_push_vec2(&app.nk, &app.nk.style.window.padding, nk_vec2(0, 0));

        if (nk_begin(&app.nk, "Drop", drop_rect,
                     NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT)) {
            nk_layout_row_dynamic(&app.nk, 20, 1);
            nk_style_push_color(&app.nk, &app.nk.style.text.color,
                                nk_rgb(102, 102, 102));
            nk_label(&app.nk, "Drag & drop image or video", NK_TEXT_CENTERED);
            nk_style_pop_color(&app.nk);

            nk_layout_row_dynamic(&app.nk, 16, 1);
            nk_style_push_color(&app.nk, &app.nk.style.text.color,
                                nk_rgb(68, 68, 68));
            nk_label(&app.nk, "S = export  F = fullscreen  Space = play/pause",
                     NK_TEXT_CENTERED);
            nk_style_pop_color(&app.nk);
        }
        nk_end(&app.nk);

        nk_style_pop_vec2(&app.nk);
        nk_style_pop_style_item(&app.nk);
    }

    /* Detect parameter changes and rebuild/reprocess as needed */
    if (app.hi_res != app.prev_hi_res) {
        app.prev_hi_res = app.hi_res;
        if (app.pending_pixels) {
            compute_cell_size(app.pending_w, app.pending_h, app.hi_res,
                              &app.cell_w, &app.cell_h);
            if (app.pm_stride != 0) {
                glif_sampling_precompute_free(&app.pm);
                app.pm_stride = 0;
            }
            /* Reset all smoothers — grid dimensions changed */
            glif_norm_smoother_init(&app.norm_sm);
            glif_shape_smoother_free(&app.shape_sm);
            glif_contrast_smoother_init(&app.contrast_sm);
            glif_match_smoother_free(&app.match_sm);
            rebuild_pipeline();
            app.pending_dirty = 1;
        }
    }
    if (app.stabilize != app.prev_stabilize) {
        app.prev_stabilize = app.stabilize;
        if (!app.stabilize) {
            /* Reset smoothers so they start fresh when re-enabled */
            glif_norm_smoother_init(&app.norm_sm);
            glif_shape_smoother_free(&app.shape_sm);
            glif_contrast_smoother_init(&app.contrast_sm);
            glif_match_smoother_free(&app.match_sm);
        }
        if (app.pending_pixels) app.pending_dirty = 1;
    }
    if (app.dir_crunch != app.prev_dir_crunch ||
        app.global_crunch != app.prev_global_crunch ||
        app.adaptive_on != app.prev_adaptive_on) {
        /* Contrast params changed — reprocess current image */
        app.prev_dir_crunch = app.dir_crunch;
        app.prev_global_crunch = app.global_crunch;
        app.prev_adaptive_on = app.adaptive_on;
        if (app.pending_pixels) app.pending_dirty = 1;
    }

    /* Process pending frame data */
    if (app.pending_dirty && app.pending_pixels) {
        process_frame(app.pending_pixels, app.pending_w,
                      app.pending_h, app.pending_channels);
        app.pending_dirty = 0;
    }

    /* Render ASCII art viewport */
    if (app.has_result && layout.viewport.width > 0) {
        vp_render(&app.grid, layout.viewport);
    }

    /* Compare divider is now drawn in the fragment shader (vp_render.h) */

    /* Compare labels */
    if (app.compare_on && app.has_result && app.content_w > 0) {
        nk_style_push_style_item(&app.nk,
            &app.nk.style.window.fixed_background,
            nk_style_item_color(nk_rgba(0, 0, 0, 178)));
        nk_style_push_vec2(&app.nk,
            &app.nk.style.window.padding, nk_vec2(8, 3));

        struct nk_rect lbl_l = nk_rect(
            app.content_x + 8, app.content_y + 8, 72, 20);
        nk_window_set_bounds(&app.nk, "CmpL", lbl_l);
        if (nk_begin(&app.nk, "CmpL", lbl_l,
                     NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT)) {
            nk_layout_row_dynamic(&app.nk, 14, 1);
            nk_label(&app.nk, "Original", NK_TEXT_LEFT);
        }
        nk_end(&app.nk);

        struct nk_rect lbl_r = nk_rect(
            app.content_x + app.content_w - 56 - 8,
            app.content_y + 8, 56, 20);
        nk_window_set_bounds(&app.nk, "CmpR", lbl_r);
        if (nk_begin(&app.nk, "CmpR", lbl_r,
                     NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT)) {
            nk_layout_row_dynamic(&app.nk, 14, 1);
            nk_label(&app.nk, "ASCII", NK_TEXT_LEFT);
        }
        nk_end(&app.nk);

        nk_style_pop_vec2(&app.nk);
        nk_style_pop_style_item(&app.nk);
    }

    /* Render Nuklear draw list (toolbar widgets) */
    glViewport(0, 0, app.canvas_w, app.canvas_h);
    nk_webgl_render(&app.nk_gl, &app.nk, logical_w, logical_h);
}

EMSCRIPTEN_KEEPALIVE
void app_mouse(int x, int y, int buttons) {
    app.mouse_x = x;
    app.mouse_y = y;
    app.mouse_buttons = buttons;
}

EMSCRIPTEN_KEEPALIVE
void app_key(int key, int down) {
    /* Forward key to Nuklear if needed */
    (void)key;
    (void)down;
}

EMSCRIPTEN_KEEPALIVE
void app_touch(int id, int x, int y, int phase) {
    /* Map primary touch to mouse */
    if (id == 0) {
        app.mouse_x = x;
        app.mouse_y = y;
        app.mouse_buttons = (phase < 2) ? 1 : 0; /* 0=begin,1=move → pressed; 2=end → released */
    }
}

EMSCRIPTEN_KEEPALIVE
void app_load_font(const uint8_t *data, int len) {
    free(app.font_data);
    app.font_data = malloc((size_t)len);
    if (!app.font_data) return;
    memcpy(app.font_data, data, (size_t)len);
    app.font_len = len;
    app.font_loaded = 1;
    rebuild_pipeline();
}

EMSCRIPTEN_KEEPALIVE
void app_load_image(const uint8_t *data, int w, int h, int channels) {
    size_t size = (size_t)w * (size_t)h * (size_t)channels;
    free(app.pending_pixels);
    app.pending_pixels = malloc(size);
    if (!app.pending_pixels) return;
    memcpy(app.pending_pixels, data, size);
    app.pending_w = w;
    app.pending_h = h;
    app.pending_channels = channels;

    /* Compute cell size from image dimensions and resolution mode */
    int new_cw, new_ch;
    compute_cell_size(w, h, app.hi_res, &new_cw, &new_ch);
    if (new_cw != app.cell_w || new_ch != app.cell_h) {
        app.cell_w = new_cw;
        app.cell_h = new_ch;
        if (app.pm_stride != 0) {
            glif_sampling_precompute_free(&app.pm);
            app.pm_stride = 0;
        }
        rebuild_pipeline();
    }

    app.pending_dirty = 1;
}

EMSCRIPTEN_KEEPALIVE
void app_video_frame(const uint8_t *data, int w, int h) {
    app_load_image(data, w, h, 4);
}

EMSCRIPTEN_KEEPALIVE
int app_get_camera_count(void) {
    /* JS handles camera enumeration; this is a placeholder */
    return 0;
}

EMSCRIPTEN_KEEPALIVE
void app_switch_camera(int index) {
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (window.glifSwitchCamera) window.glifSwitchCamera($0); }, index);
#else
    (void)index;
#endif
}

/* ── Grid data export for JS-side SVG/PPM generation ── */

EMSCRIPTEN_KEEPALIVE
int app_get_grid_rows(void) { return app.has_result ? app.grid.rows : 0; }

EMSCRIPTEN_KEEPALIVE
int app_get_grid_cols(void) { return app.has_result ? app.grid.cols : 0; }

EMSCRIPTEN_KEEPALIVE
int app_get_grid_cell_w(void) { return app.cell_w; }

EMSCRIPTEN_KEEPALIVE
int app_get_grid_cell_h(void) { return app.cell_h; }

EMSCRIPTEN_KEEPALIVE
void app_export_grid(uint8_t *out) {
    if (!app.has_result) return;
    int n = app.grid.rows * app.grid.cols;
    for (int i = 0; i < n; i++) {
        out[i * 4 + 0] = (uint8_t)app.grid.cells[i].ch;
        out[i * 4 + 1] = app.grid.cells[i].r;
        out[i * 4 + 2] = app.grid.cells[i].g;
        out[i * 4 + 3] = app.grid.cells[i].b;
    }
}

EMSCRIPTEN_KEEPALIVE
const uint8_t *app_get_font_ptr(void) { return app.font_data; }

EMSCRIPTEN_KEEPALIVE
int app_get_font_len(void) { return app.font_len; }

/* ── Compare overlay (C-side owns state; JS no longer positions overlays) ── */

EMSCRIPTEN_KEEPALIVE
void app_upload_video_frame(const uint8_t *rgba, int w, int h) {
    if (!app.initialized || !rgba || w <= 0 || h <= 0) return;
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, app.vp.video_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

/* ── Media bar state (called from JS) ── */

EMSCRIPTEN_KEEPALIVE
void app_set_content_mode(int has_content, int video_mode) {
    app.has_content = has_content;
    app.video_mode = video_mode;
    if (!has_content && app.compare_on) {
        app.compare_on = 0;
        app.vp.compare_mode = 0;
        app.compare_dragging = 0;
    }
}

EMSCRIPTEN_KEEPALIVE
void app_set_media_state(int playing, float current_time, float duration,
                         int audio_available, int audio_muted) {
    app.playing = playing;
    if (!app.seeking)
        app.current_time = current_time;
    app.duration = duration;
    app.audio_available = audio_available;
    app.audio_muted = audio_muted;
}

EMSCRIPTEN_KEEPALIVE
void app_toggle_hdr(void) {
    if (!app.has_content) return;
    app.hdr_intensity = app.hdr_intensity > 0.0f ? 0.0f : 1.0f;
}

EMSCRIPTEN_KEEPALIVE
void app_toggle_compare(void) {
    toggle_compare_mode();
}
