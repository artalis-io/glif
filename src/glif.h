#ifndef GLIF_GLIF_H
#define GLIF_GLIF_H

#include "compress.h"
#include <stddef.h>
#include <stdint.h>

/* Parsed header */
typedef struct {
    uint8_t version, flags;
    uint16_t cols, rows, cell_w, cell_h;
    float fps;
    uint32_t frames;
    uint8_t reserved[2];
} GlifHeader;

/* Per-frame index entry */
typedef struct {
    size_t offset;        /* byte offset of payload in buffer */
    uint8_t type;         /* GLIF_FRAME_* compression type */
    uint32_t payload_len;
} GlifFrameIndex;

typedef struct {
    const uint8_t *data;
    size_t len;
    GlifHeader header;
    GlifFrameIndex *index;   /* header.frames entries */
    int cells;
    uint8_t *decoded;        /* current decoded frame, cells*4 */
    uint8_t *prev;           /* for delta decode */
    uint8_t *work;           /* scratch for delta+RLE */
    int cur_frame;           /* last decoded frame, -1 = none */
    /* audio (BLIP v2 — crushed PCM) */
    int has_audio;
    uint8_t *audio_pcm;           /* decompressed crushed PCM (owned) */
    uint32_t audio_pcm_samples;   /* number of samples */
    uint16_t audio_sample_rate;   /* e.g. 8000 */
    uint8_t audio_bit_depth;      /* 4 or 8 */
    /* original audio (ORIG section) */
    int has_orig_audio;
    const uint8_t *orig_audio;     /* pointer into data buffer (not owned) */
    size_t orig_audio_len;
} GlifReader;

int glif_reader_open(GlifReader *gr, const uint8_t *data, size_t len);
int glif_reader_decode(GlifReader *gr, uint32_t frame);
const uint8_t *glif_reader_frame_data(const GlifReader *gr);
const GlifHeader *glif_reader_header(const GlifReader *gr);
void glif_reader_close(GlifReader *gr);

/* Audio access (crushed PCM) */
int glif_reader_has_audio(const GlifReader *gr);
const uint8_t *glif_reader_audio_pcm(const GlifReader *gr, uint32_t *num_samples);
uint16_t glif_reader_audio_sample_rate(const GlifReader *gr);
uint8_t glif_reader_audio_bit_depth(const GlifReader *gr);
int glif_reader_has_orig_audio(const GlifReader *gr);
const uint8_t *glif_reader_orig_audio(const GlifReader *gr, size_t *len);

#endif
