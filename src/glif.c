#include "glif.h"
#include "output.h"
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

    gr->cells = (int)gr->header.cols * (int)gr->header.rows;

    if (gr->header.frames == 0) {
        gr->index = NULL;
        return 0;
    }

    gr->index = calloc(gr->header.frames, sizeof(GlifFrameIndex));
    if (!gr->index) return -1;

    /* Build frame index */
    size_t pos = GLIF_HEADER_SIZE;

    if (gr->header.version == GLIF_VERSION_1) {
        /* v1: fixed-size raw frames */
        size_t frame_size = (size_t)gr->cells * 4;
        for (uint32_t i = 0; i < gr->header.frames; i++) {
            if (pos + frame_size > len) {
                free(gr->index);
                gr->index = NULL;
                return -1;
            }
            gr->index[i].offset = pos;
            gr->index[i].type = GLIF_FRAME_RAW;
            gr->index[i].payload_len = (uint32_t)frame_size;
            pos += frame_size;
        }
    } else {
        /* v2: walk 5-byte envelopes */
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

    return 0;
}

/* Decode a single frame given its index entry, using prev for delta reference */
static int decode_single(GlifReader *gr, uint32_t frame) {
    const GlifFrameIndex *fi = &gr->index[frame];
    const uint8_t *payload = gr->data + fi->offset;
    size_t plen = fi->payload_len;
    int cells = gr->cells;

    switch (fi->type) {
    case GLIF_FRAME_RAW:
        if (plen != (size_t)cells * 4) return -1;
        memcpy(gr->decoded, payload, plen);
        return 0;
    case GLIF_FRAME_RLE:
        return glif_compress_rle_decode(payload, plen, gr->decoded, cells);
    case GLIF_FRAME_DELTA:
        return glif_compress_delta_decode(payload, plen, gr->prev,
                                          gr->decoded, cells);
    case GLIF_FRAME_DELTA_RLE:
        return glif_compress_delta_rle_decode(payload, plen, gr->prev,
                                              gr->work, gr->decoded, cells);
    default:
        return -1;
    }
}

/* Check if a frame type is a delta frame (bit 0 set) */
static int is_delta(uint8_t type) {
    return (type & GLIF_FRAME_DELTA) != 0;
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
        /* Fast path: we already decoded past the keyframe, continue from cur_frame+1 */
        start = (uint32_t)(gr->cur_frame + 1);
    } else {
        start = keyframe;
    }

    /* Decode forward from start through target */
    for (uint32_t f = start; f <= frame; f++) {
        /* For delta frames, prev must contain the previous decoded frame.
         * For the first frame in our sequence, if it's not a delta,
         * prev doesn't matter. If start > keyframe, prev already has
         * the right data from the last decode. If start == keyframe,
         * we need prev to be valid for any delta frames after it. */
        if (f > start || (f == start && f > 0 && gr->cur_frame == (int)f - 1)) {
            /* prev is already correct from last iteration or previous call */
        } else if (f == keyframe) {
            /* First frame in sequence is the keyframe — prev not needed
             * (keyframe is non-delta by definition) */
            memset(gr->prev, 0, (size_t)gr->cells * 4);
        }

        if (decode_single(gr, f) != 0) return -1;

        /* Copy decoded to prev for next frame's delta reference */
        if (f < frame) {
            memcpy(gr->prev, gr->decoded, (size_t)gr->cells * 4);
        }
    }

    /* After successful decode, update prev and cur_frame */
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
    gr->index = NULL;
    gr->decoded = NULL;
    gr->prev = NULL;
    gr->work = NULL;
    gr->cur_frame = -1;
}
