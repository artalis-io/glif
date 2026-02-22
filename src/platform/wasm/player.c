/*
 * player.c — Standalone WASM entry point for .glif playback.
 *
 * Minimal deps: glif.c, compress.c, player.c + stb_truetype (for font atlas).
 * No pipeline code (image/grid/contrast/match).
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

/* stb_truetype implementation for this compilation unit (no font.c linked) */
#define STB_TRUETYPE_IMPLEMENTATION
#include "vp_render.h"

#include "glif.h"

/* Embedded font for player */
#include "geist_pixel_square.h"

/* ── Player state ── */

static struct {
    VpRenderState vp;
    GlifReader reader;
    float dpr;
    int canvas_w, canvas_h;
    uint8_t *char_buf;     /* cols*rows, char indices for GL upload */
    uint8_t *color_buf;    /* cols*rows*3, RGB for GL upload */
    uint8_t *glif_data;    /* retained copy of .glif buffer */
    size_t glif_len;
    float hdr_intensity;   /* 0.0 = off, 1.0 = full HDR effect */
    int loaded, initialized;
} player;

/* ── Exported WASM API ── */

EMSCRIPTEN_KEEPALIVE
void player_init(float dpr, int canvas_w, int canvas_h) {
    memset(&player, 0, sizeof(player));
    player.dpr = dpr;
    player.canvas_w = canvas_w;
    player.canvas_h = canvas_h;

#ifdef __EMSCRIPTEN__
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha = 0;
    attrs.depth = 0;
    attrs.stencil = 0;
    attrs.antialias = 0;
    attrs.majorVersion = 1;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx_handle =
        emscripten_webgl_create_context("#glif-player-canvas", &attrs);
    emscripten_webgl_make_context_current(ctx_handle);
#endif

    vp_state_init(&player.vp);

    /* Build font atlas with default cell size (will be rebuilt on load) */
    vp_build_font_atlas(&player.vp,
                        geist_pixel_square_ttf, (int)geist_pixel_square_ttf_len,
                        10, 20, dpr);

    player.initialized = 1;
}

EMSCRIPTEN_KEEPALIVE
int player_load(const uint8_t *data, int len) {
    if (!player.initialized || len <= 0) return -1;

    /* Close previous if any */
    if (player.loaded) {
        glif_reader_close(&player.reader);
        free(player.char_buf);
        free(player.color_buf);
        free(player.glif_data);
        player.char_buf = NULL;
        player.color_buf = NULL;
        player.glif_data = NULL;
        player.loaded = 0;
    }

    /* Copy buffer (caller retains ownership of original) */
    player.glif_data = malloc((size_t)len);
    if (!player.glif_data) return -1;
    memcpy(player.glif_data, data, (size_t)len);
    player.glif_len = (size_t)len;

    if (glif_reader_open(&player.reader, player.glif_data, player.glif_len) != 0) {
        free(player.glif_data);
        player.glif_data = NULL;
        return -1;
    }

    const GlifHeader *hdr = glif_reader_header(&player.reader);
    int cells = (int)hdr->cols * (int)hdr->rows;

    player.char_buf = malloc((size_t)cells);
    player.color_buf = malloc((size_t)cells * 3);
    if (!player.char_buf || !player.color_buf) {
        glif_reader_close(&player.reader);
        free(player.char_buf);
        free(player.color_buf);
        free(player.glif_data);
        player.char_buf = NULL;
        player.color_buf = NULL;
        player.glif_data = NULL;
        return -1;
    }

    /* Rebuild font atlas at render quality (4x source cell size, matching PPM
       scale). The atlas_cell_w/h drives both glyph rasterization resolution
       and the cell aspect ratio in vp_draw — scaling both by the same factor
       preserves the correct aspect while giving crisp glyphs. */
    int render_scale = 4;
    vp_build_font_atlas(&player.vp,
                        geist_pixel_square_ttf, (int)geist_pixel_square_ttf_len,
                        hdr->cell_w * render_scale,
                        hdr->cell_h * render_scale,
                        player.dpr);

    player.loaded = 1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int player_decode_frame(int frame) {
    if (!player.loaded || frame < 0) return -1;

    if (glif_reader_decode(&player.reader, (uint32_t)frame) != 0)
        return -1;

    const uint8_t *decoded = glif_reader_frame_data(&player.reader);
    const GlifHeader *hdr = glif_reader_header(&player.reader);
    int cells = (int)hdr->cols * (int)hdr->rows;

    /* Convert decoded [ch, r, g, b] → char_buf (subtract 32) + color_buf */
    for (int i = 0; i < cells; i++) {
        player.char_buf[i] = decoded[i * 4] - 32;
        player.color_buf[i * 3 + 0] = decoded[i * 4 + 1];
        player.color_buf[i * 3 + 1] = decoded[i * 4 + 2];
        player.color_buf[i * 3 + 2] = decoded[i * 4 + 3];
    }

    return 0;
}

EMSCRIPTEN_KEEPALIVE
void player_render(void) {
    if (!player.loaded || !player.initialized) return;

    const GlifHeader *hdr = glif_reader_header(&player.reader);

    glViewport(0, 0, player.canvas_w, player.canvas_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    vp_render_raw(&player.vp, player.char_buf, player.color_buf,
                  hdr->cols, hdr->rows,
                  player.canvas_w, player.canvas_h,
                  player.hdr_intensity);
}

EMSCRIPTEN_KEEPALIVE
void player_resize(int w, int h) {
    player.canvas_w = w;
    player.canvas_h = h;
}

EMSCRIPTEN_KEEPALIVE
void player_set_hdr(float intensity) {
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    player.hdr_intensity = intensity;
}

/* ── Header accessors ── */

EMSCRIPTEN_KEEPALIVE
int player_get_frames(void) {
    if (!player.loaded) return 0;
    return (int)player.reader.header.frames;
}

EMSCRIPTEN_KEEPALIVE
float player_get_fps(void) {
    if (!player.loaded) return 0.0f;
    return player.reader.header.fps;
}

EMSCRIPTEN_KEEPALIVE
int player_get_cols(void) {
    if (!player.loaded) return 0;
    return player.reader.header.cols;
}

EMSCRIPTEN_KEEPALIVE
int player_get_rows(void) {
    if (!player.loaded) return 0;
    return player.reader.header.rows;
}

EMSCRIPTEN_KEEPALIVE
int player_get_cell_w(void) {
    if (!player.loaded) return 0;
    return player.reader.header.cell_w;
}

EMSCRIPTEN_KEEPALIVE
int player_get_cell_h(void) {
    if (!player.loaded) return 0;
    return player.reader.header.cell_h;
}

EMSCRIPTEN_KEEPALIVE
int player_get_flags(void) {
    if (!player.loaded) return 0;
    return player.reader.header.flags;
}

/* ── Audio accessors (crushed PCM) ── */

EMSCRIPTEN_KEEPALIVE
int player_has_audio(void) {
    if (!player.loaded) return 0;
    return glif_reader_has_audio(&player.reader);
}

EMSCRIPTEN_KEEPALIVE
int player_get_audio_pcm_ptr(void) {
    if (!player.loaded) return 0;
    uint32_t num;
    const uint8_t *pcm = glif_reader_audio_pcm(&player.reader, &num);
    return (int)(uintptr_t)pcm;
}

EMSCRIPTEN_KEEPALIVE
int player_get_audio_pcm_len(void) {
    if (!player.loaded) return 0;
    uint32_t num = 0;
    glif_reader_audio_pcm(&player.reader, &num);
    return (int)num;
}

EMSCRIPTEN_KEEPALIVE
int player_get_audio_sample_rate(void) {
    if (!player.loaded) return 0;
    return (int)glif_reader_audio_sample_rate(&player.reader);
}

EMSCRIPTEN_KEEPALIVE
int player_get_audio_bit_depth(void) {
    if (!player.loaded) return 0;
    return (int)glif_reader_audio_bit_depth(&player.reader);
}

EMSCRIPTEN_KEEPALIVE
int player_has_orig_audio(void) {
    if (!player.loaded) return 0;
    return glif_reader_has_orig_audio(&player.reader);
}

EMSCRIPTEN_KEEPALIVE
void player_free(void) {
    if (player.loaded) {
        glif_reader_close(&player.reader);
        free(player.char_buf);
        free(player.color_buf);
        free(player.glif_data);
        player.char_buf = NULL;
        player.color_buf = NULL;
        player.glif_data = NULL;
        player.loaded = 0;
    }
}
