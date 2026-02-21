#include "utest.h"
#include "blip.h"
#include "glif.h"
#include "output.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Helper: generate a mono PCM tone ── */

static int16_t *gen_tone(int sample_rate, float freq, int samples, float amplitude) {
    int16_t *buf = malloc((size_t)samples * sizeof(int16_t));
    if (!buf) return NULL;
    for (int i = 0; i < samples; i++) {
        float t = (float)i / (float)sample_rate;
        float s = amplitude * sinf(2.0f * (float)M_PI * freq * t);
        buf[i] = (int16_t)(s * 32767.0f);
    }
    return buf;
}

/* Generate silence */
static int16_t *gen_silence(int samples) {
    int16_t *buf = calloc((size_t)samples, sizeof(int16_t));
    return buf;
}

/* ── Encoder init/free ── */

UTEST(blip, encoder_init_free) {
    BlipEncoder be;
    ASSERT_EQ(blip_encoder_init(&be, 44100, 1, 8000, 8), 0);
    ASSERT_TRUE(be.buf != NULL);
    ASSERT_EQ(be.len, (uint32_t)0);
    ASSERT_EQ(be.src_rate, 44100);
    ASSERT_EQ(be.dst_rate, 8000);
    ASSERT_EQ(be.bit_depth, 8);
    blip_encoder_free(&be);
}

UTEST(blip, encoder_init_4bit) {
    BlipEncoder be;
    ASSERT_EQ(blip_encoder_init(&be, 44100, 1, 8000, 4), 0);
    ASSERT_EQ(be.bit_depth, 4);
    blip_encoder_free(&be);
}

UTEST(blip, encoder_init_invalid) {
    BlipEncoder be;
    /* Invalid sample rates */
    ASSERT_NE(blip_encoder_init(&be, 0, 1, 8000, 8), 0);
    ASSERT_NE(blip_encoder_init(&be, 44100, 1, 0, 8), 0);
    /* Invalid channels */
    ASSERT_NE(blip_encoder_init(&be, 44100, 0, 8000, 8), 0);
    ASSERT_NE(blip_encoder_init(&be, 44100, 3, 8000, 8), 0);
    /* Invalid bit depth */
    ASSERT_NE(blip_encoder_init(&be, 44100, 1, 8000, 16), 0);
}

/* ── Resampling ── */

UTEST(blip, downsample_ratio) {
    BlipEncoder be;
    ASSERT_EQ(blip_encoder_init(&be, 44100, 1, 8000, 8), 0);

    /* Feed 1 second of silence at 44100 Hz */
    int16_t *pcm = gen_silence(44100);
    ASSERT_TRUE(pcm != NULL);
    blip_encoder_feed(&be, pcm, 44100);
    free(pcm);

    /* Should produce ~8000 output samples (± a few for rounding) */
    uint32_t n = blip_encoder_samples(&be);
    ASSERT_TRUE(n > 7900 && n < 8100);

    blip_encoder_free(&be);
}

/* ── 8-bit output range ── */

UTEST(blip, output_range_8bit) {
    BlipEncoder be;
    ASSERT_EQ(blip_encoder_init(&be, 44100, 1, 8000, 8), 0);

    /* Feed a full-amplitude sine wave */
    int16_t *pcm = gen_tone(44100, 440.0f, 44100, 1.0f);
    ASSERT_TRUE(pcm != NULL);
    blip_encoder_feed(&be, pcm, 44100);
    free(pcm);

    const uint8_t *data = blip_encoder_data(&be);
    uint32_t n = blip_encoder_samples(&be);
    ASSERT_TRUE(n > 0);

    /* All samples should be in [0, 255] (implicit for uint8_t) */
    /* Check we have both high and low values (sine wave should span the range) */
    uint8_t mn = 255, mx = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (data[i] < mn) mn = data[i];
        if (data[i] > mx) mx = data[i];
    }
    /* Full amplitude sine: expect near 0 and near 255 */
    ASSERT_TRUE(mn < 20);
    ASSERT_TRUE(mx > 235);

    blip_encoder_free(&be);
}

/* ── 4-bit output range ── */

UTEST(blip, output_range_4bit) {
    BlipEncoder be;
    ASSERT_EQ(blip_encoder_init(&be, 44100, 1, 8000, 4), 0);

    int16_t *pcm = gen_tone(44100, 440.0f, 44100, 1.0f);
    ASSERT_TRUE(pcm != NULL);
    blip_encoder_feed(&be, pcm, 44100);
    free(pcm);

    const uint8_t *data = blip_encoder_data(&be);
    uint32_t n = blip_encoder_samples(&be);
    ASSERT_TRUE(n > 0);

    /* 4-bit: all values should be in [0, 15] */
    uint8_t mn = 15, mx = 0;
    for (uint32_t i = 0; i < n; i++) {
        ASSERT_TRUE(data[i] <= 15);
        if (data[i] < mn) mn = data[i];
        if (data[i] > mx) mx = data[i];
    }
    ASSERT_TRUE(mn < 2);
    ASSERT_TRUE(mx > 13);

    blip_encoder_free(&be);
}

/* ── Silence produces centered output ── */

UTEST(blip, silence_is_centered) {
    BlipEncoder be;
    ASSERT_EQ(blip_encoder_init(&be, 44100, 1, 8000, 8), 0);

    int16_t *pcm = gen_silence(44100);
    ASSERT_TRUE(pcm != NULL);
    blip_encoder_feed(&be, pcm, 44100);
    free(pcm);

    const uint8_t *data = blip_encoder_data(&be);
    uint32_t n = blip_encoder_samples(&be);

    /* Silence (s16 zero) should map to 128 (unsigned midpoint) */
    for (uint32_t i = 0; i < n; i++) {
        ASSERT_EQ(data[i], 128);
    }

    blip_encoder_free(&be);
}

/* ── Stereo downmix ── */

UTEST(blip, stereo_downmix) {
    BlipEncoder be;
    ASSERT_EQ(blip_encoder_init(&be, 8000, 2, 8000, 8), 0);

    /* Interleaved stereo: left = +16384, right = -16384 → mono midpoint (zero → 128) */
    int16_t stereo[200];
    for (int i = 0; i < 100; i++) {
        stereo[i * 2]     = 16384;
        stereo[i * 2 + 1] = -16384;
    }
    blip_encoder_feed(&be, stereo, 200);

    const uint8_t *data = blip_encoder_data(&be);
    uint32_t n = blip_encoder_samples(&be);
    ASSERT_TRUE(n > 0);

    /* Downmixed: (16384 + -16384)/2 = 0 → unsigned 128 */
    for (uint32_t i = 0; i < n; i++) {
        ASSERT_TRUE(data[i] >= 126 && data[i] <= 130);
    }

    blip_encoder_free(&be);
}

/* ── Incremental feeding ── */

UTEST(blip, incremental_feed) {
    BlipEncoder be;
    ASSERT_EQ(blip_encoder_init(&be, 44100, 1, 8000, 8), 0);

    /* Feed in small chunks (simulating frame-by-frame feeding) */
    int chunk = 44100 / 30; /* ~30fps video frames */
    int16_t *pcm = gen_tone(44100, 440.0f, 44100, 0.5f);
    ASSERT_TRUE(pcm != NULL);

    int total_fed = 0;
    while (total_fed + chunk <= 44100) {
        blip_encoder_feed(&be, pcm + total_fed, chunk);
        total_fed += chunk;
    }
    free(pcm);

    /* Should produce approximately 8000 samples for ~1 second of audio */
    uint32_t n = blip_encoder_samples(&be);
    ASSERT_TRUE(n > 7500 && n < 8500);

    blip_encoder_free(&be);
}

/* ── Helper: read file into buffer ── */

static uint8_t *file_to_buffer(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

/* ── GlifWriter/Reader integration test ── */

UTEST(blip, encoder_writer_integration) {
    const char *path = "/tmp/glif_test_blip_audio.glif";
    int cols = 4, rows = 3;
    int sr = 44100;

    /* Create a glif writer with crushed PCM audio */
    GlifWriter gw;
    ASSERT_EQ(glif_writer_init_v2(&gw, path, cols, rows, 10, 20, 30.0f, 0, 1), 0);
    ASSERT_EQ(glif_writer_enable_audio(&gw, sr, 1, 8000, 8), 0);

    /* Write some video frames (dummy) */
    GlifGridCell cells[12];
    memset(cells, 0, sizeof(cells));
    for (int i = 0; i < 12; i++) {
        cells[i].ch = 'A';
        cells[i].r = 200; cells[i].g = 100; cells[i].b = 50;
    }
    GlifGrid grid = { .rows = rows, .cols = cols, .cell_w = 10, .cell_h = 20,
                      .cells = cells };

    glif_writer_frame(&gw, &grid);

    /* Feed some audio (440Hz sine, ~1 video frame worth at 30fps) */
    int audio_samples = sr / 30;
    int16_t *pcm = gen_tone(sr, 440.0f, audio_samples, 0.8f);
    ASSERT_TRUE(pcm != NULL);
    glif_writer_audio_samples(&gw, pcm, audio_samples);
    free(pcm);

    ASSERT_EQ(glif_writer_finish(&gw), 0);

    /* Read back */
    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);

    /* Verify video frame */
    ASSERT_EQ(gr.header.frames, (uint32_t)1);
    ASSERT_EQ(glif_reader_decode(&gr, 0), 0);

    /* Verify audio section present */
    ASSERT_TRUE(glif_reader_has_audio(&gr));
    ASSERT_TRUE(gr.audio_pcm_samples > 0);
    ASSERT_EQ(gr.audio_sample_rate, 8000);
    ASSERT_EQ(gr.audio_bit_depth, 8);

    uint32_t num = 0;
    const uint8_t *audio = glif_reader_audio_pcm(&gr, &num);
    ASSERT_TRUE(audio != NULL);
    ASSERT_TRUE(num > 0);

    /* Original audio should not be present */
    ASSERT_FALSE(glif_reader_has_orig_audio(&gr));

    glif_reader_close(&gr);
    free(buf);
    remove(path);
}

/* ── Backwards compatibility: no audio flag means no audio parsed ── */

UTEST(blip, no_audio_backwards_compat) {
    const char *path = "/tmp/glif_test_no_audio.glif";
    int cols = 4, rows = 3;

    /* Create a plain glif without audio */
    GlifWriter gw;
    ASSERT_EQ(glif_writer_init_v2(&gw, path, cols, rows, 10, 20, 30.0f, 0, 1), 0);

    GlifGridCell cells[12];
    memset(cells, 0, sizeof(cells));
    for (int i = 0; i < 12; i++) {
        cells[i].ch = 'A'; cells[i].r = 200; cells[i].g = 100; cells[i].b = 50;
    }
    GlifGrid grid = { .rows = rows, .cols = cols, .cell_w = 10, .cell_h = 20,
                      .cells = cells };
    glif_writer_frame(&gw, &grid);
    ASSERT_EQ(glif_writer_finish(&gw), 0);

    /* Read back */
    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);
    ASSERT_FALSE(glif_reader_has_audio(&gr));
    ASSERT_FALSE(glif_reader_has_orig_audio(&gr));

    glif_reader_close(&gr);
    free(buf);
    remove(path);
}

/* ── 4-bit roundtrip through writer/reader ── */

UTEST(blip, writer_reader_4bit) {
    const char *path = "/tmp/glif_test_blip_4bit.glif";
    int cols = 4, rows = 3;
    int sr = 44100;

    GlifWriter gw;
    ASSERT_EQ(glif_writer_init_v2(&gw, path, cols, rows, 10, 20, 30.0f, 0, 1), 0);
    ASSERT_EQ(glif_writer_enable_audio(&gw, sr, 1, 8000, 4), 0);

    GlifGridCell cells[12];
    memset(cells, 0, sizeof(cells));
    for (int i = 0; i < 12; i++) {
        cells[i].ch = 'A';
        cells[i].r = 200; cells[i].g = 100; cells[i].b = 50;
    }
    GlifGrid grid = { .rows = rows, .cols = cols, .cell_w = 10, .cell_h = 20,
                      .cells = cells };
    glif_writer_frame(&gw, &grid);

    int audio_samples = sr / 30;
    int16_t *pcm = gen_tone(sr, 440.0f, audio_samples, 0.8f);
    ASSERT_TRUE(pcm != NULL);
    glif_writer_audio_samples(&gw, pcm, audio_samples);
    free(pcm);

    ASSERT_EQ(glif_writer_finish(&gw), 0);

    size_t len;
    uint8_t *buf = file_to_buffer(path, &len);
    ASSERT_TRUE(buf != NULL);

    GlifReader gr;
    ASSERT_EQ(glif_reader_open(&gr, buf, len), 0);
    ASSERT_TRUE(glif_reader_has_audio(&gr));
    ASSERT_EQ(gr.audio_bit_depth, 4);
    ASSERT_EQ(gr.audio_sample_rate, 8000);

    /* 4-bit samples should be unpacked to individual bytes [0..15] */
    uint32_t num = 0;
    const uint8_t *audio = glif_reader_audio_pcm(&gr, &num);
    ASSERT_TRUE(audio != NULL);
    ASSERT_TRUE(num > 0);
    for (uint32_t i = 0; i < num; i++) {
        ASSERT_TRUE(audio[i] <= 15);
    }

    glif_reader_close(&gr);
    free(buf);
    remove(path);
}

UTEST_MAIN();
