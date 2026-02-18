/*
 * ext.c — Minimal WASM entry point for the Chrome extension.
 *
 * Reuses the Glif rendering pipeline and WebGL viewport shader from ui.c,
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

/* ── Extension state ── */

static struct {
    /* Glif pipeline */
    CharDatabase db;
    SamplingConfig sc;
    PrecomputedMasks pm;
    int cell_w, cell_h;
    float dir_crunch, global_crunch;
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
    int atlas_cell_w, atlas_cell_h;

    /* Font data */
    uint8_t *font_data;
    int font_len;
    int font_loaded;

    /* Canvas state */
    float dpr;
    int canvas_w, canvas_h;

    /* Temporal smoothing */
    NormSmoother norm_sm;
    ShapeSmoother shape_sm;
    ContrastSmoother contrast_sm;
    MatchSmoother match_sm;

    /* Adaptive contrast */
    AdaptiveContrast adaptive;

    /* Resolution mode */
    int hi_res; /* 0 = ~120 cols, 1 = ~240 cols */

    int initialized;
} ext;

/* ── Helpers (same as ui.c) ── */

static void compute_cell_size(int img_w, int *out_cw, int *out_ch) {
    int target_cols = ext.hi_res ? 240 : 120;
    int cw = img_w / target_cols;
    if (cw < 2) cw = 2;
    *out_cw = cw;
    *out_ch = cw * 2;
}

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
    ext.vp_program = glCreateProgram();
    glAttachShader(ext.vp_program, vs);
    glAttachShader(ext.vp_program, fs);
    glLinkProgram(ext.vp_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    ext.vp_quad_buf = 0;
    glGenBuffers(1, &ext.vp_quad_buf);
    glBindBuffer(GL_ARRAY_BUFFER, ext.vp_quad_buf);
    float quad[] = {
        -1, -1,  1, -1,  -1, 1,
        -1,  1,  1, -1,   1, 1
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    ext.char_tex = create_data_texture();
    ext.color_tex = create_data_texture();
}

static void vp_build_font_atlas(void) {
    if (!ext.font_data || ext.font_len <= 0) return;

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, ext.font_data, 0)) return;

    int phys_h = (int)roundf((float)ext.cell_h * ext.dpr);
    int phys_w = (int)roundf((float)ext.cell_w * ext.dpr);

    ext.atlas_cell_w = phys_w;
    ext.atlas_cell_h = phys_h;

    int atlas_w = ATLAS_GRID_X * phys_w;
    int atlas_h = ATLAS_GRID_Y * phys_h;

    uint8_t *atlas_rgba = calloc((size_t)atlas_w * (size_t)atlas_h, 4);
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

    if (!ext.atlas_tex) glGenTextures(1, &ext.atlas_tex);
    glBindTexture(GL_TEXTURE_2D, ext.atlas_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas_w, atlas_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, atlas_rgba);
    free(atlas_rgba);
}

static void vp_render(const Grid *grid) {
    if (!ext.vp_program || !ext.atlas_tex || !grid || grid->rows <= 0)
        return;

    int cols = grid->cols;
    int rows = grid->rows;
    int ncells = rows * cols;

    /* Upload char grid */
    uint8_t *char_data = malloc((size_t)ncells);
    if (!char_data) return;
    for (int i = 0; i < ncells; i++)
        char_data[i] = (uint8_t)(grid->cells[i].ch - 32);

    glBindTexture(GL_TEXTURE_2D, ext.char_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, cols, rows, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, char_data);
    free(char_data);

    /* Upload color grid */
    uint8_t *color_data = calloc((size_t)ncells, 3);
    if (!color_data) return;
    for (int i = 0; i < ncells; i++) {
        color_data[i * 3 + 0] = grid->cells[i].r;
        color_data[i * 3 + 1] = grid->cells[i].g;
        color_data[i * 3 + 2] = grid->cells[i].b;
    }
    glBindTexture(GL_TEXTURE_2D, ext.color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, cols, rows, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, color_data);
    free(color_data);

    /* Fullscreen viewport — no Clay bounds, fill the whole canvas */
    int vp_w = ext.canvas_w;
    int vp_h = ext.canvas_h;

    float grid_px_w = (float)cols * (float)ext.atlas_cell_w;
    float grid_px_h = (float)rows * (float)ext.atlas_cell_h;
    float scale_x = (float)vp_w / grid_px_w;
    float scale_y = (float)vp_h / grid_px_h;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;

    float render_cell_w = (float)ext.atlas_cell_w * scale;
    float render_cell_h = (float)ext.atlas_cell_h * scale;
    float render_w = (float)cols * render_cell_w;
    float render_h = (float)rows * render_cell_h;
    float offset_x = ((float)vp_w - render_w) * 0.5f;
    float offset_y = ((float)vp_h - render_h) * 0.5f;

    glViewport(0, 0, vp_w, vp_h);

    glUseProgram(ext.vp_program);

    GLint a_pos = glGetAttribLocation(ext.vp_program, "a_pos");
    glBindBuffer(GL_ARRAY_BUFFER, ext.vp_quad_buf);
    glEnableVertexAttribArray((GLuint)a_pos);
    glVertexAttribPointer((GLuint)a_pos, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ext.char_tex);
    glUniform1i(glGetUniformLocation(ext.vp_program, "u_charGrid"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ext.color_tex);
    glUniform1i(glGetUniformLocation(ext.vp_program, "u_colorGrid"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ext.atlas_tex);
    glUniform1i(glGetUniformLocation(ext.vp_program, "u_fontAtlas"), 2);

    glUniform2f(glGetUniformLocation(ext.vp_program, "u_gridSize"),
                (float)cols, (float)rows);
    glUniform2f(glGetUniformLocation(ext.vp_program, "u_atlasGrid"),
                (float)ATLAS_GRID_X, (float)ATLAS_GRID_Y);
    glUniform2f(glGetUniformLocation(ext.vp_program, "u_cellSize"),
                render_cell_w, render_cell_h);
    glUniform2f(glGetUniformLocation(ext.vp_program, "u_resolution"),
                (float)vp_w, (float)vp_h);
    glUniform2f(glGetUniformLocation(ext.vp_program, "u_offset"),
                offset_x, offset_y);

    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray((GLuint)a_pos);
}

/* ── Pipeline rebuild ── */

static void rebuild_pipeline(void) {
    if (!ext.font_loaded) return;

    char_db_free(&ext.db);
    if (ext.pm_stride != 0) {
        sampling_precompute_free(&ext.pm);
        ext.pm_stride = 0;
    }

    sampling_config_init(&ext.sc);
    if (char_db_create_from_memory(&ext.db, ext.font_data, (size_t)ext.font_len,
                                   ext.cell_w, ext.cell_h, &ext.sc) != 0) {
        ext.font_loaded = 0;
        return;
    }

    vp_build_font_atlas();
}

static void process_frame(const uint8_t *pixels, int w, int h) {
    if (!ext.font_loaded) return;

    Image img;
    if (image_load_buffer(&img, pixels, w, h, 4) != 0) return;

    /* Recompute cell size if image width changed */
    int new_cw, new_ch;
    compute_cell_size(w, &new_cw, &new_ch);
    if (new_cw != ext.cell_w || new_ch != ext.cell_h) {
        ext.cell_w = new_cw;
        ext.cell_h = new_ch;
        if (ext.pm_stride != 0) {
            sampling_precompute_free(&ext.pm);
            ext.pm_stride = 0;
        }
        /* Reset smoothers — grid dimensions changed */
        norm_smoother_init(&ext.norm_sm);
        shape_smoother_free(&ext.shape_sm);
        contrast_smoother_init(&ext.contrast_sm);
        match_smoother_free(&ext.match_sm);
        rebuild_pipeline();
    }

    /* Rebuild masks if stride changed */
    if (ext.pm_stride != w) {
        if (ext.pm_stride != 0) sampling_precompute_free(&ext.pm);
        if (sampling_precompute(&ext.pm, &ext.sc, ext.cell_w, ext.cell_h, w) != 0)
            return;
        ext.pm_stride = w;
    }

    LightnessMap lm;
    if (lightness_map_create(&lm, &img) != 0) return;

    if (ext.has_result) grid_free(&ext.grid);

    if (grid_create(&ext.grid, &img, ext.cell_w, ext.cell_h) != 0) {
        lightness_map_free(&lm);
        return;
    }

    /* Always use temporal smoothing for video */
    norm_smoother_apply(&ext.norm_sm, &lm, 0.4f);
    grid_compute_vectors_fast(&ext.grid, &lm, &ext.pm);
    shape_smoother_apply(&ext.shape_sm, &ext.grid, 0.4f);
    grid_compute_colors(&ext.grid, &img);
    contrast_analyze_frame(&ext.adaptive, &ext.grid);
    contrast_smoother_apply(&ext.contrast_sm, &ext.adaptive, 0.3f);
    contrast_directional(&ext.grid, &ext.sc, ext.dir_crunch, &ext.adaptive);
    contrast_global(&ext.grid, ext.global_crunch, &ext.adaptive);
    match_smoother_apply(&ext.match_sm, &ext.grid, &ext.db, 0.15f);

    ext.has_result = 1;
    lightness_map_free(&lm);
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

    vp_init();
    sampling_config_init(&ext.sc);

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
        sampling_precompute_free(&ext.pm);
        ext.pm_stride = 0;
    }
    ext.cell_w = 10;
    ext.cell_h = 20;

    norm_smoother_init(&ext.norm_sm);
    shape_smoother_free(&ext.shape_sm);
    contrast_smoother_init(&ext.contrast_sm);
    match_smoother_free(&ext.match_sm);

    if (ext.has_result) {
        grid_free(&ext.grid);
        ext.has_result = 0;
    }
}
