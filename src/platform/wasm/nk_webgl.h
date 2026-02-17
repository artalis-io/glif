#ifndef NK_WEBGL_H
#define NK_WEBGL_H

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_STANDARD_IO
#include "nuklear.h"

typedef struct {
    /* OpenGL ES 2.0 handles */
    unsigned int program;
    unsigned int vbo, ebo;
    unsigned int font_tex;
    /* Shader uniform/attribute locations */
    int a_pos, a_uv, a_col;
    int u_proj, u_tex;
    /* Device pixel ratio */
    float dpr;
    /* Logical canvas size */
    int width, height;
    /* Nuklear draw config */
    struct nk_draw_null_texture tex_null;
    struct nk_buffer cmds;
} NkWebGL;

void nk_webgl_init(NkWebGL *nk, float dpr);
void nk_webgl_font_stash_begin(NkWebGL *nk, struct nk_font_atlas *atlas);
void nk_webgl_font_stash_end(NkWebGL *nk, struct nk_font_atlas *atlas);
void nk_webgl_render(NkWebGL *nk, struct nk_context *ctx, int width, int height);
void nk_webgl_free(NkWebGL *nk);

#endif /* NK_WEBGL_H */
