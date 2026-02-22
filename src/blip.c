#include "blip.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── Crushed PCM encoder ── */

int blip_encoder_init(BlipEncoder *be, int src_rate, int channels,
                      int dst_rate, int bit_depth) {
    memset(be, 0, sizeof(*be));
    if (src_rate <= 0 || dst_rate <= 0 || channels < 1 || channels > 2)
        return -1;
    if (bit_depth != 4 && bit_depth != 8) return -1;

    be->src_rate = src_rate;
    be->dst_rate = dst_rate;
    be->bit_depth = bit_depth;
    be->channels = channels;
    be->resample_pos = 0.0;

    /* Pre-allocate ~10 seconds of output (with overflow check) */
    if (dst_rate > (int)(UINT32_MAX / 10)) return -1;
    be->cap = (uint32_t)dst_rate * 10;
    be->buf = malloc(be->cap);
    if (!be->buf) return -1;
    be->len = 0;

    return 0;
}

/* Grow output buffer if needed */
static int ensure_cap(BlipEncoder *be, uint32_t extra) {
    if (extra > UINT32_MAX - be->len) return -1; /* addition overflow */
    uint32_t needed = be->len + extra;
    if (needed <= be->cap) return 0;
    uint32_t new_cap = be->cap;
    while (new_cap < needed) {
        if (new_cap > UINT32_MAX / 2) return -1; /* doubling overflow */
        new_cap *= 2;
    }
    uint8_t *nb = realloc(be->buf, new_cap);
    if (!nb) return -1;
    be->buf = nb;
    be->cap = new_cap;
    return 0;
}

/* Get a mono sample from PCM (handling stereo downmix) */
static float get_mono_sample(const int16_t *pcm, int idx, int channels, int total) {
    if (channels == 2) {
        int i = idx * 2;
        if (i + 1 < total) {
            return ((float)pcm[i] + (float)pcm[i + 1]) * 0.5f;
        }
        return (float)pcm[i];
    }
    return (float)pcm[idx];
}

void blip_encoder_feed(BlipEncoder *be, const int16_t *pcm, int samples) {
    if (!be || !pcm || samples <= 0) return;

    int mono_samples = (be->channels == 2) ? samples / 2 : samples;
    double ratio = (double)be->src_rate / (double)be->dst_rate;

    /* Estimate output samples and ensure capacity */
    uint32_t est = (uint32_t)((double)mono_samples / ratio) + 2;
    if (ensure_cap(be, est) != 0) return;

    while (be->resample_pos < (double)(mono_samples - 1)) {
        /* Linear interpolation */
        int idx = (int)be->resample_pos;
        double frac = be->resample_pos - (double)idx;

        float s0 = get_mono_sample(pcm, idx, be->channels, samples);
        float s1 = get_mono_sample(pcm, idx + 1, be->channels, samples);
        float s = s0 + (float)frac * (s1 - s0);

        /* Convert s16 [-32768, 32767] → unsigned 8-bit [0, 255] */
        float norm = (s / 32768.0f + 1.0f) * 0.5f; /* [0, 1] */
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;

        uint8_t out;
        if (be->bit_depth == 8) {
            out = (uint8_t)(norm * 255.0f + 0.5f);
        } else {
            /* 4-bit: quantize to 16 levels, store in full byte
             * (packing happens at write time) */
            int v4 = (int)(norm * 15.0f + 0.5f);
            if (v4 > 15) v4 = 15;
            out = (uint8_t)v4;
        }

        if (ensure_cap(be, 1) != 0) return;
        be->buf[be->len++] = out;
        be->resample_pos += ratio;
    }

    /* Adjust position for next call (subtract consumed input samples) */
    be->resample_pos -= (double)mono_samples;
    if (be->resample_pos < 0.0) be->resample_pos = 0.0;
}

void blip_encoder_free(BlipEncoder *be) {
    if (!be) return;
    free(be->buf);
    be->buf = NULL;
    be->len = 0;
    be->cap = 0;
}

const uint8_t *blip_encoder_data(const BlipEncoder *be) {
    if (!be) return NULL;
    return be->buf;
}

uint32_t blip_encoder_samples(const BlipEncoder *be) {
    if (!be) return 0;
    return be->len;
}

int blip_encoder_bit_depth(const BlipEncoder *be) {
    if (!be) return 0;
    return be->bit_depth;
}

int blip_encoder_dst_rate(const BlipEncoder *be) {
    if (!be) return 0;
    return be->dst_rate;
}
