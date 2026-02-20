#ifndef GLIF_VP_RENDER_H
#define GLIF_VP_RENDER_H

/*
 * vp_render.h — Shared WebGL font-atlas rendering for WASM targets.
 *
 * Header-only (each WASM target is compiled separately, never linked together).
 * All functions are static inline.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <GLES2/gl2.h>
#include "stb_truetype.h"

/* ── Shader sources ── */

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
    "uniform float u_hdrIntensity;\n"
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
    "    if (u_hdrIntensity > 0.0) {\n"
    "        float gamma = mix(1.0, 0.8, u_hdrIntensity);\n"
    "        color = pow(color, vec3(gamma));\n"
    "        color = mix(color, smoothstep(0.0, 1.0, color), u_hdrIntensity * 0.5);\n"
    "        float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));\n"
    "        color = mix(vec3(luma), color, 1.0 + u_hdrIntensity * 0.3);\n"
    "    }\n"
    "    gl_FragColor = vec4(color * alpha, 1.0);\n"
    "}\n";

/* ── Atlas constants ── */

#define VP_ATLAS_GRID_X 16
#define VP_ATLAS_GRID_Y 6
#define VP_ATLAS_CHARS  95

/* ── Render state ── */

typedef struct {
    GLuint program;
    GLuint quad_buf;
    GLuint atlas_tex;
    GLuint char_tex;
    GLuint color_tex;
    int atlas_cell_w, atlas_cell_h;
} VpRenderState;

/* ── Helpers ── */

static inline GLuint vp_compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

static inline GLuint vp_create_data_texture(void) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

/* ── Init ── */

static inline void vp_state_init(VpRenderState *vp) {
    /* Textures may have non-power-of-two widths (e.g. 213 cols).
       Default GL_UNPACK_ALIGNMENT=4 causes row padding that shears the image. */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    GLuint vs = vp_compile_shader(GL_VERTEX_SHADER, vp_vert_src);
    GLuint fs = vp_compile_shader(GL_FRAGMENT_SHADER, vp_frag_src);
    vp->program = glCreateProgram();
    glAttachShader(vp->program, vs);
    glAttachShader(vp->program, fs);
    glLinkProgram(vp->program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    vp->quad_buf = 0;
    glGenBuffers(1, &vp->quad_buf);
    glBindBuffer(GL_ARRAY_BUFFER, vp->quad_buf);
    float quad[] = {
        -1, -1,  1, -1,  -1, 1,
        -1,  1,  1, -1,   1, 1
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    vp->atlas_tex = 0;
    vp->char_tex = vp_create_data_texture();
    vp->color_tex = vp_create_data_texture();
    vp->atlas_cell_w = 0;
    vp->atlas_cell_h = 0;
}

/* ── Font atlas ── */

static inline void vp_build_font_atlas(VpRenderState *vp,
                                        const uint8_t *font_data, int font_len,
                                        int cell_w, int cell_h, float dpr) {
    if (!font_data || font_len <= 0) return;

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, font_data, 0)) return;

    int phys_h = (int)roundf((float)cell_h * dpr);
    int phys_w = (int)roundf((float)cell_w * dpr);

    vp->atlas_cell_w = phys_w;
    vp->atlas_cell_h = phys_h;

    int atlas_w = VP_ATLAS_GRID_X * phys_w;
    int atlas_h = VP_ATLAS_GRID_Y * phys_h;

    uint8_t *atlas_rgba = calloc((size_t)atlas_w * (size_t)atlas_h, 4);
    if (!atlas_rgba) return;

    float scale = stbtt_ScaleForPixelHeight(&font, (float)phys_h);

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    int baseline = (int)((float)ascent * scale);

    for (int i = 0; i < VP_ATLAS_CHARS; i++) {
        int ch = 32 + i;
        int col = i % VP_ATLAS_GRID_X;
        int row = i / VP_ATLAS_GRID_X;

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

    if (!vp->atlas_tex) glGenTextures(1, &vp->atlas_tex);
    glBindTexture(GL_TEXTURE_2D, vp->atlas_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas_w, atlas_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, atlas_rgba);
    free(atlas_rgba);
}

/* ── Internal draw call (shared between render_grid and render_raw) ── */

static inline void vp_draw(VpRenderState *vp,
                            int cols, int rows,
                            int vp_w, int vp_h,
                            float hdr_intensity) {
    float grid_px_w = (float)cols * (float)vp->atlas_cell_w;
    float grid_px_h = (float)rows * (float)vp->atlas_cell_h;
    float scale_x = (float)vp_w / grid_px_w;
    float scale_y = (float)vp_h / grid_px_h;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;

    float render_cell_w = (float)vp->atlas_cell_w * scale;
    float render_cell_h = (float)vp->atlas_cell_h * scale;
    float render_w = (float)cols * render_cell_w;
    float render_h = (float)rows * render_cell_h;
    float offset_x = ((float)vp_w - render_w) * 0.5f;
    float offset_y = ((float)vp_h - render_h) * 0.5f;

    glUseProgram(vp->program);

    GLint a_pos = glGetAttribLocation(vp->program, "a_pos");
    glBindBuffer(GL_ARRAY_BUFFER, vp->quad_buf);
    glEnableVertexAttribArray((GLuint)a_pos);
    glVertexAttribPointer((GLuint)a_pos, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, vp->char_tex);
    glUniform1i(glGetUniformLocation(vp->program, "u_charGrid"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, vp->color_tex);
    glUniform1i(glGetUniformLocation(vp->program, "u_colorGrid"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, vp->atlas_tex);
    glUniform1i(glGetUniformLocation(vp->program, "u_fontAtlas"), 2);

    glUniform2f(glGetUniformLocation(vp->program, "u_gridSize"),
                (float)cols, (float)rows);
    glUniform2f(glGetUniformLocation(vp->program, "u_atlasGrid"),
                (float)VP_ATLAS_GRID_X, (float)VP_ATLAS_GRID_Y);
    glUniform2f(glGetUniformLocation(vp->program, "u_cellSize"),
                render_cell_w, render_cell_h);
    glUniform2f(glGetUniformLocation(vp->program, "u_resolution"),
                (float)vp_w, (float)vp_h);
    glUniform2f(glGetUniformLocation(vp->program, "u_offset"),
                offset_x, offset_y);
    glUniform1f(glGetUniformLocation(vp->program, "u_hdrIntensity"),
                hdr_intensity);

    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray((GLuint)a_pos);
}

/* ── Upload char/color data to textures (for callers that set their own viewport) ── */

static inline void vp_upload(VpRenderState *vp,
                              const uint8_t *char_data,
                              const uint8_t *color_data,
                              int cols, int rows) {
    glBindTexture(GL_TEXTURE_2D, vp->char_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, cols, rows, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, char_data);

    glBindTexture(GL_TEXTURE_2D, vp->color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, cols, rows, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, color_data);
}

/* ── Render from pre-prepared buffers (fullscreen viewport) ── */

static inline void vp_render_raw(VpRenderState *vp,
                                  const uint8_t *char_data,
                                  const uint8_t *color_data,
                                  int cols, int rows,
                                  int canvas_w, int canvas_h,
                                  float hdr_intensity) {
    if (!vp->program || !vp->atlas_tex || cols <= 0 || rows <= 0)
        return;

    /* Upload char grid (LUMINANCE: char indices 0-94) */
    glBindTexture(GL_TEXTURE_2D, vp->char_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, cols, rows, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, char_data);

    /* Upload color grid (RGB) */
    glBindTexture(GL_TEXTURE_2D, vp->color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, cols, rows, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, color_data);

    glViewport(0, 0, canvas_w, canvas_h);
    vp_draw(vp, cols, rows, canvas_w, canvas_h, hdr_intensity);
}

#endif /* GLIF_VP_RENDER_H */
