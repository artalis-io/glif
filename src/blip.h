#ifndef GLIF_BLIP_H
#define GLIF_BLIP_H

#include <stddef.h>
#include <stdint.h>

#define BLIP_MAGIC   "BLIP"
#define BLIP_VERSION 2

/* ── Crushed PCM encoder ──
 * Downsamples and quantizes audio to retro 8-bit unsigned PCM.
 * Default: 8000 Hz mono, 8-bit (telephone quality, very 1980s). */

typedef struct BlipEncoder {
    int src_rate;       /* input sample rate (e.g. 44100) */
    int dst_rate;       /* output sample rate (e.g. 8000) */
    int bit_depth;      /* output bits per sample (4 or 8) */
    int channels;       /* input channels (1 or 2, downmixed to mono) */
    double resample_pos;/* fractional position in input stream */
    /* Output buffer (crushed unsigned PCM) */
    uint8_t *buf;
    uint32_t len;       /* number of output samples written */
    uint32_t cap;       /* capacity in samples */
} BlipEncoder;

int  blip_encoder_init(BlipEncoder *be, int src_rate, int channels,
                       int dst_rate, int bit_depth);
void blip_encoder_feed(BlipEncoder *be, const int16_t *pcm, int samples);
void blip_encoder_free(BlipEncoder *be);

/* Access crushed PCM data */
const uint8_t *blip_encoder_data(const BlipEncoder *be);
uint32_t blip_encoder_samples(const BlipEncoder *be);
int blip_encoder_bit_depth(const BlipEncoder *be);
int blip_encoder_dst_rate(const BlipEncoder *be);

#endif
