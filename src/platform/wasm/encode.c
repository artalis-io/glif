/*
 * encode.c — Headless WASM entry point for .glif encoding.
 *
 * Full pipeline: image → lightness → grid → contrast → match → GlifWriter.
 * No UI, no WebGL. Designed to run in a Web Worker.
 */

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
#include "output.h"
#include "blip.h"

/* Embedded font for encoder */
#include "geist_pixel_square.h"

/* ── Encoder state ── */

static struct {
    /* Pipeline objects */
    GlifCharDatabase db;
    GlifSamplingConfig sc;
    GlifPrecomputedMasks pm;
    GlifGrid grid;
    GlifLightnessMap lm;
    GlifImage img;
    GlifAdaptiveContrast adaptive;

    /* Params */
    int cell_w, cell_h;
    float dir_crunch, global_crunch;
    int dark_mode;
    int compress;

    /* Frame buffer (owned, reused per frame) */
    uint8_t *frame_buf;
    size_t frame_size;

    /* Writer: writes to in-memory buffer */
    GlifWriter writer;
    int writer_active;

    /* Output buffer (accumulated .glif binary) */
    uint8_t *output;
    size_t output_len;

    int initialized;
    int pipeline_ready; /* 1 after first frame sets up grid/lm/pm */
} enc;

/* ── Exported WASM API ── */

EMSCRIPTEN_KEEPALIVE
int encoder_init(int w, int h, int cell_w, int cell_h, float fps, int dark,
                 int compress, const unsigned char *font_data_ptr,
                 int font_data_len, float dir_crunch, float global_crunch,
                 int audio_rate, int audio_depth) {
    memset(&enc, 0, sizeof(enc));

    enc.cell_w = cell_w;
    enc.cell_h = cell_h;
    enc.dir_crunch = dir_crunch;
    enc.global_crunch = global_crunch;
    enc.dark_mode = dark;
    enc.compress = compress;
    enc.adaptive.floor = -1.0f; /* disabled by default */

    /* Init sampling geometry */
    glif_sampling_config_init(&enc.sc);

    /* Load font */
    const unsigned char *font = font_data_ptr;
    size_t font_len = (size_t)font_data_len;
    if (!font || font_len == 0) {
        font = geist_pixel_square_ttf;
        font_len = geist_pixel_square_ttf_len;
    }
    if (glif_char_db_create_from_memory(&enc.db, font, font_len,
                                         cell_w, cell_h, &enc.sc) != 0)
        return -1;

    /* Allocate frame buffer */
    enc.frame_size = (size_t)w * (size_t)h * 3;
    enc.frame_buf = malloc(enc.frame_size);
    if (!enc.frame_buf) return -1;

    /* Set up image struct (wraps frame_buf, no ownership) */
    enc.img.width = w;
    enc.img.height = h;
    enc.img.channels = 3;
    enc.img.pixels = enc.frame_buf;
    enc.img.owns_pixels = 0;

    /* Create grid */
    if (glif_grid_create(&enc.grid, &enc.img, cell_w, cell_h) != 0) {
        free(enc.frame_buf);
        return -1;
    }

    /* Precompute sampling masks */
    if (glif_sampling_precompute(&enc.pm, &enc.sc, cell_w, cell_h, w) != 0) {
        glif_grid_free(&enc.grid);
        free(enc.frame_buf);
        return -1;
    }

    /* Allocate lightness map */
    size_t npix = (size_t)w * (size_t)h;
    enc.lm.width = w;
    enc.lm.height = h;
    enc.lm.data = malloc(npix * sizeof(float));
    if (!enc.lm.data) {
        glif_sampling_precompute_free(&enc.pm);
        glif_grid_free(&enc.grid);
        free(enc.frame_buf);
        return -1;
    }

    /* Init writer to temp file */
    int cols = enc.grid.cols;
    int rows = enc.grid.rows;
    if (glif_writer_init_v2(&enc.writer, "/tmp/glif_encode.glif",
                             cols, rows, cell_w, cell_h,
                             fps, dark, compress) != 0) {
        free(enc.lm.data);
        glif_sampling_precompute_free(&enc.pm);
        glif_grid_free(&enc.grid);
        free(enc.frame_buf);
        return -1;
    }
    enc.writer_active = 1;

    /* Set compression defaults */
    if (compress) {
        glif_writer_set_quant(&enc.writer, 2); /* 6-bit */
        glif_writer_set_threshold(&enc.writer, 4);
    }

    /* Enable audio if requested */
    if (audio_rate > 0 && audio_depth > 0) {
        glif_writer_enable_audio(&enc.writer, 44100, 1,
                                  audio_rate, audio_depth);
    }

    enc.initialized = 1;
    enc.pipeline_ready = 1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int encoder_frame(const uint8_t *rgb24_ptr, int width, int height) {
    if (!enc.initialized) return -1;

    size_t sz = (size_t)width * (size_t)height * 3;
    if (sz != enc.frame_size) return -1;

    /* Copy frame data into our buffer */
    memcpy(enc.frame_buf, rgb24_ptr, sz);

    /* Run full pipeline */
    if (glif_lightness_map_update(&enc.lm, &enc.img) != 0) return -1;

    glif_grid_compute_vectors_fast(&enc.grid, &enc.lm, &enc.pm);
    glif_grid_compute_colors(&enc.grid, &enc.img);
    glif_contrast_analyze_frame(&enc.adaptive, &enc.grid);
    glif_contrast_directional(&enc.grid, &enc.sc, enc.dir_crunch, &enc.adaptive);
    glif_contrast_global(&enc.grid, enc.global_crunch, &enc.adaptive);
    glif_match_grid(&enc.grid, &enc.db);

    /* Write frame to .glif */
    glif_writer_frame(&enc.writer, &enc.grid);

    return 0;
}

EMSCRIPTEN_KEEPALIVE
void encoder_audio_samples(const int16_t *pcm_ptr, int num_samples) {
    if (!enc.initialized || !enc.writer.blip) return;
    glif_writer_audio_samples(&enc.writer, pcm_ptr, num_samples);
}

EMSCRIPTEN_KEEPALIVE
void encoder_keep_original_audio(const uint8_t *data_ptr, int len) {
    if (!enc.initialized || len <= 0) return;
    glif_writer_keep_original_audio(&enc.writer, data_ptr, (size_t)len);
}

EMSCRIPTEN_KEEPALIVE
int encoder_finish(void) {
    if (!enc.initialized || !enc.writer_active) return -1;

    glif_writer_finish(&enc.writer);
    enc.writer_active = 0;

    /* Read the temp file into output buffer */
    FILE *f = fopen("/tmp/glif_encode.glif", "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return -1; }

    free(enc.output);
    enc.output = malloc((size_t)size);
    if (!enc.output) { fclose(f); return -1; }

    if (fread(enc.output, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(enc.output);
        enc.output = NULL;
        enc.output_len = 0;
        return -1;
    }
    fclose(f);
    enc.output_len = (size_t)size;

    /* Cleanup pipeline */
    free(enc.lm.data); enc.lm.data = NULL;
    glif_sampling_precompute_free(&enc.pm);
    glif_grid_free(&enc.grid);
    free(enc.frame_buf); enc.frame_buf = NULL;
    glif_char_db_free(&enc.db);
    enc.initialized = 0;

    return 0;
}

EMSCRIPTEN_KEEPALIVE
uint8_t *encoder_get_output_ptr(void) {
    return enc.output;
}

EMSCRIPTEN_KEEPALIVE
int encoder_get_output_len(void) {
    return (int)enc.output_len;
}
