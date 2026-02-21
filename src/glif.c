#include "glif.h"
#include "output.h"
#include "miniz.h"
#include <stdlib.h>
#include <string.h>

static uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float rd_f32(const uint8_t *p) {
    uint32_t bits = rd_u32(p);
    float v;
    memcpy(&v, &bits, 4);
    return v;
}

int glif_reader_open(GlifReader *gr, const uint8_t *data, size_t len) {
    memset(gr, 0, sizeof(*gr));
    gr->cur_frame = -1;

    if (len < GLIF_HEADER_SIZE) return -1;
    if (memcmp(data, GLIF_MAGIC, 4) != 0) return -1;

    gr->data = data;
    gr->len = len;

    gr->header.version = data[4];
    gr->header.flags   = data[5];
    gr->header.cols    = rd_u16(data + 6);
    gr->header.rows    = rd_u16(data + 8);
    gr->header.cell_w  = rd_u16(data + 10);
    gr->header.cell_h  = rd_u16(data + 12);
    gr->header.fps     = rd_f32(data + 14);
    gr->header.frames  = rd_u32(data + 18);
    gr->header.reserved[0] = data[22];
    gr->header.reserved[1] = data[23];

    gr->cells = (int)gr->header.cols * (int)gr->header.rows;

    if (gr->header.frames == 0) {
        gr->index = NULL;
        return 0;
    }

    gr->index = calloc(gr->header.frames, sizeof(GlifFrameIndex));
    if (!gr->index) return -1;

    /* Build frame index */
    size_t pos = GLIF_HEADER_SIZE;

    if (!(gr->header.flags & GLIF_FLAG_COMPRESSED)) {
        /* Uncompressed: fixed-size raw frames */
        size_t frame_size = (size_t)gr->cells * 4;
        for (uint32_t i = 0; i < gr->header.frames; i++) {
            if (pos + frame_size > len) {
                free(gr->index);
                gr->index = NULL;
                return -1;
            }
            gr->index[i].offset = pos;
            gr->index[i].type = 0xFF;  /* raw (no codec) */
            gr->index[i].payload_len = (uint32_t)frame_size;
            pos += frame_size;
        }
    } else {
        /* Compressed: walk 5-byte envelopes */
        for (uint32_t i = 0; i < gr->header.frames; i++) {
            if (pos + 5 > len) {
                free(gr->index);
                gr->index = NULL;
                return -1;
            }
            uint8_t type = data[pos];
            uint32_t payload_len = rd_u32(data + pos + 1);
            pos += 5;
            if (pos + payload_len > len) {
                free(gr->index);
                gr->index = NULL;
                return -1;
            }
            gr->index[i].offset = pos;
            gr->index[i].type = type;
            gr->index[i].payload_len = payload_len;
            pos += payload_len;
        }
    }

    /* Allocate decode buffers */
    size_t buf_size = (size_t)gr->cells * 4;
    gr->decoded = malloc(buf_size);
    gr->prev = malloc(buf_size);
    gr->work = malloc(buf_size);
    if (!gr->decoded || !gr->prev || !gr->work) {
        glif_reader_close(gr);
        return -1;
    }

    /* Parse optional BLIP v2 section after video frames */
    if ((gr->header.flags & 0x04) && pos + 16 <= len) {
        if (memcmp(data + pos, "BLIP", 4) == 0) {
            uint8_t blip_version = data[pos + 4];
            if (blip_version == 2) {
                /* BLIP v2: crushed PCM */
                gr->audio_bit_depth = data[pos + 5];
                gr->audio_sample_rate = rd_u16(data + pos + 6);
                uint32_t num_samples = rd_u32(data + pos + 8);
                uint32_t data_size = rd_u32(data + pos + 12);
                pos += 16;

                if (pos + data_size <= len && num_samples > 0) {
                    /* Compute raw size */
                    uint32_t raw_size = num_samples;
                    if (gr->audio_bit_depth == 4) {
                        raw_size = (num_samples + 1) / 2;
                    }

                    /* Decompress */
                    uint8_t *raw = malloc(raw_size);
                    if (raw) {
                        mz_ulong out_len = (mz_ulong)raw_size;
                        int mz_ret = mz_uncompress(raw, &out_len,
                                                   data + pos, (mz_ulong)data_size);
                        if (mz_ret == MZ_OK && out_len == raw_size) {
                            if (gr->audio_bit_depth == 4) {
                                /* Unpack 4-bit to individual bytes */
                                gr->audio_pcm = malloc(num_samples);
                                if (gr->audio_pcm) {
                                    for (uint32_t i = 0; i < num_samples; i++) {
                                        uint8_t byte = raw[i / 2];
                                        gr->audio_pcm[i] = (i % 2 == 0)
                                            ? (byte >> 4) : (byte & 0x0F);
                                    }
                                    gr->audio_pcm_samples = num_samples;
                                    gr->has_audio = 1;
                                }
                                free(raw);
                            } else {
                                /* 8-bit: raw data is ready to use */
                                gr->audio_pcm = raw;
                                gr->audio_pcm_samples = num_samples;
                                gr->has_audio = 1;
                            }
                        } else {
                            free(raw);
                        }
                    }
                }
                pos += data_size;
            } else {
                /* BLIP v1 (legacy): skip entire section */
                /* v1 header: 20 bytes, then palette + frames */
                if (pos + 20 <= len) {
                    uint16_t palette_size = rd_u16(data + pos + 6);
                    uint32_t frame_count = rd_u32(data + pos + 16);
                    pos += 20;
                    pos += (size_t)palette_size * 8;
                    pos += (size_t)frame_count * 4;
                }
            }
        }
    }

    /* Parse optional ORIG section */
    if (pos + 12 <= len && memcmp(data + pos, "ORIG", 4) == 0) {
        /* uint32_t codec = rd_u32(data + pos + 4); */
        uint32_t orig_size = rd_u32(data + pos + 8);
        pos += 12;
        if (pos + orig_size <= len) {
            gr->orig_audio = data + pos;
            gr->orig_audio_len = orig_size;
            gr->has_orig_audio = 1;
            pos += orig_size;
        }
    }

    return 0;
}

/* Decode a single frame given its index entry, using prev for delta reference */
static int decode_single(GlifReader *gr, uint32_t frame) {
    const GlifFrameIndex *fi = &gr->index[frame];
    const uint8_t *payload = gr->data + fi->offset;
    size_t plen = fi->payload_len;
    int cells = gr->cells;

    switch (fi->type) {
    case 0xFF:  /* raw (uncompressed) */
        if (plen != (size_t)cells * 4) return -1;
        memcpy(gr->decoded, payload, plen);
        return 0;
    case GLIF_FRAME_DEFLATE:
        return glif_compress_deflate_decode(payload, plen, gr->decoded, cells);
    case GLIF_FRAME_DELTA_DEFLATE:
        return glif_compress_delta_deflate_decode(payload, plen, gr->prev,
                                                   gr->work, gr->decoded, cells);
    case GLIF_FRAME_FILTERED_DEFLATE:
        return glif_compress_filtered_deflate_decode(payload, plen, gr->decoded,
                                                      gr->header.cols, gr->header.rows);
    case GLIF_FRAME_DELTA_FILTERED_DEFLATE:
        return glif_compress_delta_filtered_deflate_decode(payload, plen, gr->prev,
                                                            gr->decoded,
                                                            gr->header.cols, gr->header.rows);
    case GLIF_FRAME_PALETTE_DEFLATE:
        return glif_compress_palette_deflate_decode(payload, plen, gr->decoded, cells);
    case GLIF_FRAME_PLANAR_DEFLATE:
        return glif_compress_planar_deflate_decode(payload, plen, gr->decoded, cells);
    case GLIF_FRAME_DELTA_PLANAR_DEFLATE:
        return glif_compress_delta_planar_deflate_decode(payload, plen, gr->prev,
                                                          gr->decoded, cells);
    default:
        return -1;
    }
}

/* Check if a frame type is a delta frame (bit 0 set) */
static int is_delta(uint8_t type) {
    return (type & 0x01) != 0;
}

int glif_reader_decode(GlifReader *gr, uint32_t frame) {
    if (!gr->index || frame >= gr->header.frames) return -1;

    /* Find nearest preceding non-delta frame (keyframe) */
    uint32_t keyframe = frame;
    while (keyframe > 0 && is_delta(gr->index[keyframe].type))
        keyframe--;

    /* Determine starting point for sequential decode */
    uint32_t start;
    if (gr->cur_frame >= (int)keyframe && gr->cur_frame < (int)frame) {
        start = (uint32_t)(gr->cur_frame + 1);
    } else {
        start = keyframe;
    }

    /* Decode forward from start through target */
    for (uint32_t f = start; f <= frame; f++) {
        if (f > start || (f == start && f > 0 && gr->cur_frame == (int)f - 1)) {
            /* prev is already correct */
        } else if (f == keyframe) {
            memset(gr->prev, 0, (size_t)gr->cells * 4);
        }

        if (decode_single(gr, f) != 0) return -1;

        if (f < frame) {
            memcpy(gr->prev, gr->decoded, (size_t)gr->cells * 4);
        }
    }

    memcpy(gr->prev, gr->decoded, (size_t)gr->cells * 4);
    gr->cur_frame = (int)frame;

    return 0;
}

const uint8_t *glif_reader_frame_data(const GlifReader *gr) {
    if (gr->cur_frame < 0) return NULL;
    return gr->decoded;
}

const GlifHeader *glif_reader_header(const GlifReader *gr) {
    return &gr->header;
}

void glif_reader_close(GlifReader *gr) {
    free(gr->index);
    free(gr->decoded);
    free(gr->prev);
    free(gr->work);
    free(gr->audio_pcm);
    gr->index = NULL;
    gr->decoded = NULL;
    gr->prev = NULL;
    gr->work = NULL;
    gr->audio_pcm = NULL;
    gr->cur_frame = -1;
    gr->has_audio = 0;
    gr->has_orig_audio = 0;
}

int glif_reader_has_audio(const GlifReader *gr) {
    return gr->has_audio;
}

const uint8_t *glif_reader_audio_pcm(const GlifReader *gr, uint32_t *num_samples) {
    if (!gr->has_audio) return NULL;
    if (num_samples) *num_samples = gr->audio_pcm_samples;
    return gr->audio_pcm;
}

uint16_t glif_reader_audio_sample_rate(const GlifReader *gr) {
    return gr->audio_sample_rate;
}

uint8_t glif_reader_audio_bit_depth(const GlifReader *gr) {
    return gr->audio_bit_depth;
}

int glif_reader_has_orig_audio(const GlifReader *gr) {
    return gr->has_orig_audio;
}

const uint8_t *glif_reader_orig_audio(const GlifReader *gr, size_t *len) {
    if (!gr->has_orig_audio) return NULL;
    if (len) *len = gr->orig_audio_len;
    return gr->orig_audio;
}
