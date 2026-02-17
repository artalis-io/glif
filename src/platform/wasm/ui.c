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

/* ── Viewport WebGL shader (ASCII art rendering) ── */

static const char *vp_vert_src =
    "attribute vec2 a_pos;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *vp_frag_src =
    "precision mediump float;\n"
    "uniform sampler2D u_charGrid;\n"
    "uniform sampler2D u_colorGrid;\n"
    "uniform sampler2D u_fontAtlas;\n"
    "uniform vec2 u_gridSize;\n"
    "uniform vec2 u_atlasGrid;\n"
    "uniform vec2 u_cellSize;\n"
    "uniform vec2 u_resolution;\n"
    "uniform vec2 u_offset;\n"
    "void main() {\n"
    "    vec2 fragCoord = vec2(gl_FragCoord.x, u_resolution.y - gl_FragCoord.y);\n"
    "    vec2 gridCoord = fragCoord - u_offset;\n"
    "    vec2 cellCoord = floor(gridCoord / u_cellSize);\n"
    "    if (cellCoord.x < 0.0 || cellCoord.y < 0.0 ||\n"
    "        cellCoord.x >= u_gridSize.x || cellCoord.y >= u_gridSize.y) discard;\n"
    "    vec2 cellUV = (cellCoord + 0.5) / u_gridSize;\n"
    "    float charIdx = texture2D(u_charGrid, cellUV).r * 255.0;\n"
    "    vec3 color = texture2D(u_colorGrid, cellUV).rgb;\n"
    "    vec2 inCell = fract(gridCoord / u_cellSize);\n"
    "    float col = mod(charIdx, u_atlasGrid.x);\n"
    "    float row = floor(charIdx / u_atlasGrid.x);\n"
    "    vec2 atlasUV = (vec2(col, row) + inCell) / u_atlasGrid;\n"
    "    float alpha = texture2D(u_fontAtlas, atlasUV).a;\n"
    "    gl_FragColor = vec4(color * alpha, 1.0);\n"
    "}\n";

/* ── Font atlas (stb_truetype for viewport) ── */
#include "stb_truetype.h"

#define ATLAS_GRID_X 16
#define ATLAS_GRID_Y 6
#define ATLAS_CHARS  95

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
    CharDatabase db;
    SamplingConfig sc;
    PrecomputedMasks pm;
    int cell_w, cell_h;
    float dir_crunch, global_crunch;
    AdaptiveContrast adaptive;
    int adaptive_on;
    int pm_stride;

    /* Current frame result */
    Grid grid;
    int has_result;

    /* Viewport WebGL */
    GLuint vp_program;
    GLuint vp_quad_buf;
    GLuint atlas_tex;
    GLuint char_tex;
    GLuint color_tex;
    int atlas_cell_w, atlas_cell_h; /* physical pixel sizes */

    /* Font data */
    uint8_t *font_data;
    int font_len;
    int font_loaded;

    /* Canvas state */
    float dpr;
    int canvas_w, canvas_h; /* physical pixels */

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
    NormSmoother norm_sm;
    ShapeSmoother shape_sm;
    ContrastSmoother contrast_sm;
    MatchSmoother match_sm;

    /* Previous parameter values for change detection */
    float prev_dir_crunch, prev_global_crunch;
    int prev_adaptive_on;
    int prev_hi_res;
    int prev_stabilize;
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

/* ── Viewport shader helpers ── */

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

static GLuint create_data_texture(void) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

static void vp_init(void) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vp_vert_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, vp_frag_src);
    app.vp_program = glCreateProgram();
    glAttachShader(app.vp_program, vs);
    glAttachShader(app.vp_program, fs);
    glLinkProgram(app.vp_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    /* Fullscreen quad */
    app.vp_quad_buf = 0;
    glGenBuffers(1, &app.vp_quad_buf);
    glBindBuffer(GL_ARRAY_BUFFER, app.vp_quad_buf);
    float quad[] = {
        -1, -1,  1, -1,  -1, 1,
        -1,  1,  1, -1,   1, 1
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    app.char_tex = create_data_texture();
    app.color_tex = create_data_texture();
}

static void vp_build_font_atlas(void) {
    if (!app.font_data || app.font_len <= 0) return;

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, app.font_data, 0)) return;

    int cell_h = app.cell_h;
    int cell_w = app.cell_w;
    int phys_h = (int)roundf((float)cell_h * app.dpr);
    int phys_w = (int)roundf((float)cell_w * app.dpr);

    app.atlas_cell_w = phys_w;
    app.atlas_cell_h = phys_h;

    int atlas_w = ATLAS_GRID_X * phys_w;
    int atlas_h = ATLAS_GRID_Y * phys_h;

    /* RGBA atlas for WebGL */
    uint8_t *atlas_rgba = calloc((size_t)(atlas_w * atlas_h * 4), 1);
    if (!atlas_rgba) return;

    float scale = stbtt_ScaleForPixelHeight(&font, (float)phys_h);

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    int baseline = (int)((float)ascent * scale);

    for (int i = 0; i < ATLAS_CHARS; i++) {
        int ch = 32 + i;
        int col = i % ATLAS_GRID_X;
        int row = i / ATLAS_GRID_X;

        int gw, gh, xoff, yoff;
        unsigned char *bmp = stbtt_GetCodepointBitmap(&font, 0, scale,
                                                       ch, &gw, &gh, &xoff, &yoff);
        if (!bmp) continue;

        int ox = col * phys_w + xoff;
        int oy = row * phys_h + baseline + yoff;

        for (int y = 0; y < gh; y++) {
            for (int x = 0; x < gw; x++) {
                int px = ox + x;
                int py = oy + y;
                if (px < 0 || px >= atlas_w || py < 0 || py >= atlas_h) continue;
                uint8_t v = bmp[y * gw + x];
                int idx = (py * atlas_w + px) * 4;
                atlas_rgba[idx + 0] = 255;
                atlas_rgba[idx + 1] = 255;
                atlas_rgba[idx + 2] = 255;
                atlas_rgba[idx + 3] = v;
            }
        }
        stbtt_FreeBitmap(bmp, NULL);
    }

    /* Upload atlas texture */
    if (!app.atlas_tex) glGenTextures(1, &app.atlas_tex);
    glBindTexture(GL_TEXTURE_2D, app.atlas_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas_w, atlas_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, atlas_rgba);
    free(atlas_rgba);
}

static void vp_render(const Grid *grid, Clay_BoundingBox bounds) {
    if (!app.vp_program || !app.atlas_tex || !grid || grid->rows <= 0)
        return;

    int cols = grid->cols;
    int rows = grid->rows;
    int ncells = rows * cols;

    /* Upload char grid */
    uint8_t *char_data = malloc((size_t)ncells);
    if (!char_data) return;
    for (int i = 0; i < ncells; i++)
        char_data[i] = (uint8_t)(grid->cells[i].ch - 32);

    glBindTexture(GL_TEXTURE_2D, app.char_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, cols, rows, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, char_data);
    free(char_data);

    /* Upload color grid */
    uint8_t *color_data = malloc((size_t)(ncells * 3));
    if (!color_data) return;
    for (int i = 0; i < ncells; i++) {
        color_data[i * 3 + 0] = grid->cells[i].r;
        color_data[i * 3 + 1] = grid->cells[i].g;
        color_data[i * 3 + 2] = grid->cells[i].b;
    }
    glBindTexture(GL_TEXTURE_2D, app.color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, cols, rows, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, color_data);
    free(color_data);

    /* Viewport scissor (physical pixels) */
    int vp_x = (int)(bounds.x * app.dpr);
    int vp_y = (int)((float)app.canvas_h - (bounds.y + bounds.height) * app.dpr);
    int vp_w = (int)(bounds.width * app.dpr);
    int vp_h = (int)(bounds.height * app.dpr);

    /* Aspect-preserving fit: scale grid to fill viewport */
    float grid_px_w = (float)cols * (float)app.atlas_cell_w;
    float grid_px_h = (float)rows * (float)app.atlas_cell_h;
    float scale_x = (float)vp_w / grid_px_w;
    float scale_y = (float)vp_h / grid_px_h;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;

    float render_cell_w = (float)app.atlas_cell_w * scale;
    float render_cell_h = (float)app.atlas_cell_h * scale;
    float render_w = (float)cols * render_cell_w;
    float render_h = (float)rows * render_cell_h;
    float offset_x = ((float)vp_w - render_w) * 0.5f;
    float offset_y = ((float)vp_h - render_h) * 0.5f;

    glEnable(GL_SCISSOR_TEST);
    glScissor(vp_x, vp_y, vp_w, vp_h);
    glViewport(vp_x, vp_y, vp_w, vp_h);

    glUseProgram(app.vp_program);

    GLint a_pos = glGetAttribLocation(app.vp_program, "a_pos");
    glBindBuffer(GL_ARRAY_BUFFER, app.vp_quad_buf);
    glEnableVertexAttribArray((GLuint)a_pos);
    glVertexAttribPointer((GLuint)a_pos, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    /* Bind textures */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.char_tex);
    glUniform1i(glGetUniformLocation(app.vp_program, "u_charGrid"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, app.color_tex);
    glUniform1i(glGetUniformLocation(app.vp_program, "u_colorGrid"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, app.atlas_tex);
    glUniform1i(glGetUniformLocation(app.vp_program, "u_fontAtlas"), 2);

    /* Uniforms */
    glUniform2f(glGetUniformLocation(app.vp_program, "u_gridSize"),
                (float)cols, (float)rows);
    glUniform2f(glGetUniformLocation(app.vp_program, "u_atlasGrid"),
                (float)ATLAS_GRID_X, (float)ATLAS_GRID_Y);
    glUniform2f(glGetUniformLocation(app.vp_program, "u_cellSize"),
                render_cell_w, render_cell_h);
    glUniform2f(glGetUniformLocation(app.vp_program, "u_resolution"),
                (float)vp_w, (float)vp_h);
    glUniform2f(glGetUniformLocation(app.vp_program, "u_offset"),
                offset_x, offset_y);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisableVertexAttribArray((GLuint)a_pos);
    glDisable(GL_SCISSOR_TEST);
}

/* ── Pipeline rebuild ── */

static void rebuild_pipeline(void) {
    if (!app.font_loaded) return;

    char_db_free(&app.db);
    if (app.pm_stride != 0) {
        sampling_precompute_free(&app.pm);
        app.pm_stride = 0;
    }

    sampling_config_init(&app.sc);
    if (char_db_create_from_memory(&app.db, app.font_data, (size_t)app.font_len,
                                   app.cell_w, app.cell_h, &app.sc) != 0) {
        app.font_loaded = 0;
        return;
    }

    vp_build_font_atlas();
}

static void process_frame(const uint8_t *pixels, int w, int h, int channels) {
    if (!app.font_loaded) return;

    Image img;
    if (image_load_buffer(&img, pixels, w, h, channels) != 0) return;

    /* Rebuild masks if stride changed */
    if (app.pm_stride != w) {
        if (app.pm_stride != 0) sampling_precompute_free(&app.pm);
        if (sampling_precompute(&app.pm, &app.sc, app.cell_w, app.cell_h, w) != 0)
            return;
        app.pm_stride = w;
    }

    LightnessMap lm;
    if (lightness_map_create(&lm, &img) != 0) return;

    if (app.has_result) grid_free(&app.grid);

    if (grid_create(&app.grid, &img, app.cell_w, app.cell_h) != 0) {
        lightness_map_free(&lm);
        return;
    }

    if (app.stabilize)
        norm_smoother_apply(&app.norm_sm, &lm, 0.4f);
    else
        lightness_map_normalize(&lm);

    /* Temporarily disable adaptive per-cell crunch when toggle is off */
    float saved_floor = app.adaptive.floor;
    if (!app.adaptive_on)
        app.adaptive.floor = -1.0f;

    grid_compute_vectors_fast(&app.grid, &lm, &app.pm);

    if (app.stabilize)
        shape_smoother_apply(&app.shape_sm, &app.grid, 0.4f);

    grid_compute_colors(&app.grid, &img);
    contrast_analyze_frame(&app.adaptive, &app.grid);

    if (app.stabilize)
        contrast_smoother_apply(&app.contrast_sm, &app.adaptive, 0.3f);

    contrast_directional(&app.grid, &app.sc, app.dir_crunch, &app.adaptive);
    contrast_global(&app.grid, app.global_crunch, &app.adaptive);

    app.adaptive.floor = saved_floor;

    if (app.stabilize)
        match_smoother_apply(&app.match_sm, &app.grid, &app.db, 0.15f);
    else
        match_grid(&app.grid, &app.db);

    app.has_result = 1;
    lightness_map_free(&lm);
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
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(clay_mem_size, app.clay_mem);
    Clay_ErrorHandler err = { 0 };
    app.clay_ctx = Clay_Initialize(arena,
        (Clay_Dimensions){ (float)canvas_w / dpr, (float)canvas_h / dpr }, err);
    Clay_SetMeasureTextFunction(ui_measure_text, NULL);

    /* Init viewport shader */
    vp_init();

    sampling_config_init(&app.sc);

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
    vp_build_font_atlas();
}

EMSCRIPTEN_KEEPALIVE
void app_frame(void) {
    if (!app.initialized) return;
    if (app.canvas_w <= 0 || app.canvas_h <= 0 || app.dpr <= 0.0f) return;

    int logical_w = (int)((float)app.canvas_w / app.dpr);
    int logical_h = (int)((float)app.canvas_h / app.dpr);
    if (logical_w <= 0 || logical_h <= 0) return;

    /* Nuklear input */
    nk_input_begin(&app.nk);
    nk_input_motion(&app.nk, app.mouse_x, app.mouse_y);
    nk_input_button(&app.nk, NK_BUTTON_LEFT,
                    app.mouse_x, app.mouse_y,
                    (app.mouse_buttons & 1) != 0);
    nk_input_end(&app.nk);

    /* Clay layout */
    Clay_SetPointerState((Clay_Vector2){ (float)app.mouse_x, (float)app.mouse_y },
                         (app.mouse_buttons & 1) != 0);
    Clay_BeginLayout();
    UiLayout layout;
    ui_layout_build(&layout, logical_w, logical_h);
    Clay_RenderCommandArray commands = Clay_EndLayout();
    /* Get bounds AFTER EndLayout computes positions */
    ui_layout_get_bounds(&layout);

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

    /* Detect parameter changes and rebuild/reprocess as needed */
    if (app.hi_res != app.prev_hi_res) {
        app.prev_hi_res = app.hi_res;
        if (app.pending_pixels) {
            compute_cell_size(app.pending_w, app.pending_h, app.hi_res,
                              &app.cell_w, &app.cell_h);
            if (app.pm_stride != 0) {
                sampling_precompute_free(&app.pm);
                app.pm_stride = 0;
            }
            /* Reset all smoothers — grid dimensions changed */
            norm_smoother_init(&app.norm_sm);
            shape_smoother_free(&app.shape_sm);
            contrast_smoother_init(&app.contrast_sm);
            match_smoother_free(&app.match_sm);
            rebuild_pipeline();
            app.pending_dirty = 1;
        }
    }
    if (app.stabilize != app.prev_stabilize) {
        app.prev_stabilize = app.stabilize;
        if (!app.stabilize) {
            /* Reset smoothers so they start fresh when re-enabled */
            norm_smoother_init(&app.norm_sm);
            shape_smoother_free(&app.shape_sm);
            contrast_smoother_init(&app.contrast_sm);
            match_smoother_free(&app.match_sm);
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
            sampling_precompute_free(&app.pm);
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
