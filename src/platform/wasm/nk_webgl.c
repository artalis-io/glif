#include "nk_webgl.h"

#include <stddef.h>
#include <string.h>
#include <GLES2/gl2.h>

/* Vertex format matching Nuklear's output */
struct nk_vertex {
    float pos[2];
    float uv[2];
    unsigned char col[4];
};

static const char *vert_src =
    "uniform mat4 u_proj;\n"
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "attribute vec4 a_col;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_col = a_col;\n"
    "    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *frag_src =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "void main() {\n"
    "    gl_FragColor = v_col * texture2D(u_tex, v_uv);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

void nk_webgl_init(NkWebGL *nk, float dpr) {
    memset(nk, 0, sizeof(*nk));
    nk->dpr = dpr;

    /* Compile shaders */
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src);

    nk->program = glCreateProgram();
    glAttachShader(nk->program, vs);
    glAttachShader(nk->program, fs);
    glLinkProgram(nk->program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    nk->a_pos = glGetAttribLocation(nk->program, "a_pos");
    nk->a_uv  = glGetAttribLocation(nk->program, "a_uv");
    nk->a_col = glGetAttribLocation(nk->program, "a_col");
    nk->u_proj = glGetUniformLocation(nk->program, "u_proj");
    nk->u_tex  = glGetUniformLocation(nk->program, "u_tex");

    /* Create buffers */
    glGenBuffers(1, &nk->vbo);
    glGenBuffers(1, &nk->ebo);

    nk_buffer_init_default(&nk->cmds);
}

void nk_webgl_font_stash_begin(NkWebGL *nk, struct nk_font_atlas *atlas) {
    (void)nk;
    nk_font_atlas_init_default(atlas);
    nk_font_atlas_begin(atlas);
}

void nk_webgl_font_stash_end(NkWebGL *nk, struct nk_font_atlas *atlas) {
    int w, h;
    const void *pixels = nk_font_atlas_bake(atlas, &w, &h, NK_FONT_ATLAS_RGBA32);

    /* Upload atlas texture */
    glGenTextures(1, &nk->font_tex);
    glBindTexture(GL_TEXTURE_2D, nk->font_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    nk_handle tex;
    tex.id = (int)nk->font_tex;
    nk_font_atlas_end(atlas, tex, &nk->tex_null);
}

void nk_webgl_render(NkWebGL *nk, struct nk_context *ctx, int width, int height) {
    if (width <= 0 || height <= 0) { nk_clear(ctx); return; }

    /* Orthographic projection: top-left origin */
    float proj[16] = {
         2.0f/(float)width,  0.0f,                0.0f, 0.0f,
         0.0f,              -2.0f/(float)height,   0.0f, 0.0f,
         0.0f,               0.0f,               -1.0f, 0.0f,
        -1.0f,               1.0f,                0.0f, 1.0f,
    };

    /* Convert Nuklear draw commands to vertex buffers */
    static const struct nk_draw_vertex_layout_element vertex_layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT,    offsetof(struct nk_vertex, pos)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,    offsetof(struct nk_vertex, uv)},
        {NK_VERTEX_COLOR,    NK_FORMAT_R8G8B8A8, offsetof(struct nk_vertex, col)},
        {NK_VERTEX_LAYOUT_END}
    };

    struct nk_convert_config cfg = {0};
    cfg.global_alpha = 1.0f;
    cfg.shape_AA = NK_ANTI_ALIASING_ON;
    cfg.line_AA = NK_ANTI_ALIASING_ON;
    cfg.circle_segment_count = 22;
    cfg.curve_segment_count = 22;
    cfg.arc_segment_count = 22;
    cfg.tex_null = nk->tex_null;
    cfg.vertex_layout = vertex_layout;
    cfg.vertex_size = sizeof(struct nk_vertex);
    cfg.vertex_alignment = NK_ALIGNOF(struct nk_vertex);

    struct nk_buffer vbuf, ebuf;
    nk_buffer_init_default(&vbuf);
    nk_buffer_init_default(&ebuf);
    nk_convert(ctx, &nk->cmds, &vbuf, &ebuf, &cfg);

    /* Upload vertex and index data */
    const void *vertices = nk_buffer_memory_const(&vbuf);
    const void *elements = nk_buffer_memory_const(&ebuf);

    glBindBuffer(GL_ARRAY_BUFFER, nk->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vbuf.allocated, vertices, GL_STREAM_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, nk->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)ebuf.allocated, elements, GL_STREAM_DRAW);

    /* Set GL state */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);

    glUseProgram(nk->program);
    glUniformMatrix4fv(nk->u_proj, 1, GL_FALSE, proj);
    glUniform1i(nk->u_tex, 0);

    /* Set up vertex attributes */
    glBindBuffer(GL_ARRAY_BUFFER, nk->vbo);
    glEnableVertexAttribArray((GLuint)nk->a_pos);
    glEnableVertexAttribArray((GLuint)nk->a_uv);
    glEnableVertexAttribArray((GLuint)nk->a_col);

    glVertexAttribPointer((GLuint)nk->a_pos, 2, GL_FLOAT, GL_FALSE,
        sizeof(struct nk_vertex), (void *)offsetof(struct nk_vertex, pos));
    glVertexAttribPointer((GLuint)nk->a_uv, 2, GL_FLOAT, GL_FALSE,
        sizeof(struct nk_vertex), (void *)offsetof(struct nk_vertex, uv));
    glVertexAttribPointer((GLuint)nk->a_col, 4, GL_UNSIGNED_BYTE, GL_TRUE,
        sizeof(struct nk_vertex), (void *)offsetof(struct nk_vertex, col));

    /* Draw each command */
    const nk_draw_index *offset = NULL;
    const struct nk_draw_command *cmd;
    nk_draw_foreach(cmd, ctx, &nk->cmds) {
        if (!cmd->elem_count) continue;

        /* Scissor */
        glScissor(
            (GLint)(cmd->clip_rect.x * nk->dpr),
            (GLint)((float)(height - (int)(cmd->clip_rect.y + cmd->clip_rect.h)) * nk->dpr),
            (GLsizei)(cmd->clip_rect.w * nk->dpr),
            (GLsizei)(cmd->clip_rect.h * nk->dpr)
        );

        /* Bind texture */
        glBindTexture(GL_TEXTURE_2D, (GLuint)cmd->texture.id);

        glDrawElements(GL_TRIANGLES, (GLsizei)cmd->elem_count,
                       GL_UNSIGNED_SHORT, offset);
        offset += cmd->elem_count;
    }

    /* Restore state */
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisableVertexAttribArray((GLuint)nk->a_pos);
    glDisableVertexAttribArray((GLuint)nk->a_uv);
    glDisableVertexAttribArray((GLuint)nk->a_col);

    nk_buffer_free(&vbuf);
    nk_buffer_free(&ebuf);
    nk_buffer_clear(&nk->cmds);
    nk_clear(ctx);
}

void nk_webgl_free(NkWebGL *nk) {
    if (nk->program) glDeleteProgram(nk->program);
    if (nk->vbo) glDeleteBuffers(1, &nk->vbo);
    if (nk->ebo) glDeleteBuffers(1, &nk->ebo);
    if (nk->font_tex) glDeleteTextures(1, &nk->font_tex);
    nk_buffer_free(&nk->cmds);
    memset(nk, 0, sizeof(*nk));
}
