#include "compress.h"
#include "miniz.h"
#include <stdlib.h>
#include <string.h>

size_t glif_compress_rle_bound(int cells) {
    return (size_t)cells * 5;
}

size_t glif_compress_delta_bound(int cells) {
    return 4 + (size_t)cells * 6;
}

/* RLE encode: sequence of [count(u8, 1-255), ch, r, g, b] runs. */
int glif_compress_rle_encode(const uint8_t *cur, int cells,
                             uint8_t *out, size_t cap) {
    if (cells <= 0) return 0;
    size_t pos = 0;
    int i = 0;
    while (i < cells) {
        uint8_t ch = cur[i * 4];
        uint8_t r  = cur[i * 4 + 1];
        uint8_t g  = cur[i * 4 + 2];
        uint8_t b  = cur[i * 4 + 3];
        int run = 1;
        while (i + run < cells &&
               cur[(i + run) * 4]     == ch &&
               cur[(i + run) * 4 + 1] == r &&
               cur[(i + run) * 4 + 2] == g &&
               cur[(i + run) * 4 + 3] == b) {
            run++;
        }
        /* Emit runs of up to 255 */
        int remaining = run;
        while (remaining > 0) {
            int emit = remaining > 255 ? 255 : remaining;
            if (pos + 5 > cap) return -1;
            out[pos++] = (uint8_t)emit;
            out[pos++] = ch;
            out[pos++] = r;
            out[pos++] = g;
            out[pos++] = b;
            remaining -= emit;
        }
        i += run;
    }
    return (int)pos;
}

/* Delta encode: [num_changes(u32 LE), then per change: index(u16 LE), ch, r, g, b] */
int glif_compress_delta_encode(const uint8_t *cur, const uint8_t *prev,
                               int cells, uint8_t *out, size_t cap) {
    if (cells > 65535) return -1;

    uint32_t num = 0;
    for (int i = 0; i < cells; i++) {
        if (cur[i * 4]     != prev[i * 4] ||
            cur[i * 4 + 1] != prev[i * 4 + 1] ||
            cur[i * 4 + 2] != prev[i * 4 + 2] ||
            cur[i * 4 + 3] != prev[i * 4 + 3]) {
            num++;
        }
    }
    size_t needed = 4 + (size_t)num * 6;
    if (needed > cap) return -1;

    out[0] = (uint8_t)(num);
    out[1] = (uint8_t)(num >> 8);
    out[2] = (uint8_t)(num >> 16);
    out[3] = (uint8_t)(num >> 24);
    size_t pos = 4;

    for (int i = 0; i < cells; i++) {
        if (cur[i * 4]     != prev[i * 4] ||
            cur[i * 4 + 1] != prev[i * 4 + 1] ||
            cur[i * 4 + 2] != prev[i * 4 + 2] ||
            cur[i * 4 + 3] != prev[i * 4 + 3]) {
            out[pos++] = (uint8_t)(i & 0xFF);
            out[pos++] = (uint8_t)(i >> 8);
            out[pos++] = cur[i * 4];
            out[pos++] = cur[i * 4 + 1];
            out[pos++] = cur[i * 4 + 2];
            out[pos++] = cur[i * 4 + 3];
        }
    }
    return (int)pos;
}

/* Delta+RLE: XOR each cell with prev, then RLE the result */
int glif_compress_delta_rle_encode(const uint8_t *cur, const uint8_t *prev,
                                   int cells, uint8_t *work,
                                   uint8_t *out, size_t cap) {
    for (int i = 0; i < cells * 4; i++) {
        work[i] = cur[i] ^ prev[i];
    }
    return glif_compress_rle_encode(work, cells, out, cap);
}

/* RLE decode */
int glif_compress_rle_decode(const uint8_t *in, size_t len,
                             uint8_t *out, int cells) {
    size_t pos = 0;
    int decoded = 0;
    while (pos < len) {
        if (pos + 5 > len) return -1;
        int count = in[pos];
        if (count == 0) return -1;
        if (decoded + count > cells) return -1;
        uint8_t ch = in[pos + 1];
        uint8_t r  = in[pos + 2];
        uint8_t g  = in[pos + 3];
        uint8_t b  = in[pos + 4];
        for (int j = 0; j < count; j++) {
            out[(decoded + j) * 4]     = ch;
            out[(decoded + j) * 4 + 1] = r;
            out[(decoded + j) * 4 + 2] = g;
            out[(decoded + j) * 4 + 3] = b;
        }
        decoded += count;
        pos += 5;
    }
    if (decoded != cells) return -1;
    return 0;
}

/* Delta decode */
int glif_compress_delta_decode(const uint8_t *in, size_t len,
                               const uint8_t *prev,
                               uint8_t *out, int cells) {
    if (len < 4) return -1;
    uint32_t num = (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
                   ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
    if (4 + (size_t)num * 6 != len) return -1;

    memcpy(out, prev, (size_t)cells * 4);

    size_t pos = 4;
    for (uint32_t k = 0; k < num; k++) {
        uint16_t idx = (uint16_t)(in[pos] | (in[pos + 1] << 8));
        if (idx >= (uint16_t)cells) return -1;
        out[idx * 4]     = in[pos + 2];
        out[idx * 4 + 1] = in[pos + 3];
        out[idx * 4 + 2] = in[pos + 4];
        out[idx * 4 + 3] = in[pos + 5];
        pos += 6;
    }
    return 0;
}

/* Delta+RLE decode: RLE decode to get XOR, then XOR with prev */
int glif_compress_delta_rle_decode(const uint8_t *in, size_t len,
                                   const uint8_t *prev, uint8_t *work,
                                   uint8_t *out, int cells) {
    if (glif_compress_rle_decode(in, len, work, cells) != 0)
        return -1;
    for (int i = 0; i < cells * 4; i++) {
        out[i] = work[i] ^ prev[i];
    }
    return 0;
}

/* ---- Byte-level RLE ---- */

size_t glif_compress_byte_rle_bound(int len) {
    /* Worst case: every byte unique → 2 bytes per value */
    return (size_t)len * 2;
}

int glif_compress_byte_rle_encode(const uint8_t *in, int len, uint8_t *out, size_t cap) {
    if (len <= 0) return 0;
    size_t pos = 0;
    int i = 0;
    while (i < len) {
        uint8_t val = in[i];
        int run = 1;
        while (i + run < len && in[i + run] == val && run < 255)
            run++;
        if (pos + 2 > cap) return -1;
        out[pos++] = (uint8_t)run;
        out[pos++] = val;
        i += run;
    }
    return (int)pos;
}

int glif_compress_byte_rle_decode(const uint8_t *in, size_t in_len, uint8_t *out, int len) {
    size_t pos = 0;
    int decoded = 0;
    while (pos < in_len) {
        if (pos + 2 > in_len) return -1;
        int count = in[pos];
        if (count == 0) return -1;
        uint8_t val = in[pos + 1];
        if (decoded + count > len) return -1;
        memset(out + decoded, val, (size_t)count);
        decoded += count;
        pos += 2;
    }
    if (decoded != len) return -1;
    return 0;
}

/* ---- Planar helpers ---- */

/* Deinterleave [ch,r,g,b, ch,r,g,b, ...] into 4 planes of `cells` bytes each.
 * planes must point to 4*cells bytes: [ch_plane | r_plane | g_plane | b_plane] */
static void deinterleave(const uint8_t *interleaved, int cells, uint8_t *planes) {
    uint8_t *ch_p = planes;
    uint8_t *r_p  = planes + cells;
    uint8_t *g_p  = planes + cells * 2;
    uint8_t *b_p  = planes + cells * 3;
    for (int i = 0; i < cells; i++) {
        ch_p[i] = interleaved[i * 4];
        r_p[i]  = interleaved[i * 4 + 1];
        g_p[i]  = interleaved[i * 4 + 2];
        b_p[i]  = interleaved[i * 4 + 3];
    }
}

/* Interleave 4 planes back into [ch,r,g,b, ...] */
static void interleave(const uint8_t *planes, int cells, uint8_t *interleaved) {
    const uint8_t *ch_p = planes;
    const uint8_t *r_p  = planes + cells;
    const uint8_t *g_p  = planes + cells * 2;
    const uint8_t *b_p  = planes + cells * 3;
    for (int i = 0; i < cells; i++) {
        interleaved[i * 4]     = ch_p[i];
        interleaved[i * 4 + 1] = r_p[i];
        interleaved[i * 4 + 2] = g_p[i];
        interleaved[i * 4 + 3] = b_p[i];
    }
}

/* ---- Planar RLE (keyframe) ---- */

size_t glif_compress_planar_rle_bound(int cells) {
    /* 4 planes × (4 bytes length header + worst-case byte RLE) */
    return 4 * (4 + glif_compress_byte_rle_bound(cells));
}

int glif_compress_planar_rle_encode(const uint8_t *cur, int cells, uint8_t *out, size_t cap) {
    if (cells <= 0) return 0;
    /* Need scratch for deinterleaved planes — use stack for small, alloc for large */
    size_t plane_bytes = (size_t)cells * 4;
    uint8_t *planes = malloc(plane_bytes);
    if (!planes) return -1;

    deinterleave(cur, cells, planes);

    size_t pos = 0;
    for (int p = 0; p < 4; p++) {
        const uint8_t *plane = planes + (size_t)p * cells;
        if (pos + 4 > cap) { free(planes); return -1; }
        /* Reserve space for length, encode, then write length */
        size_t len_offset = pos;
        pos += 4;
        int encoded = glif_compress_byte_rle_encode(plane, cells, out + pos, cap - pos);
        if (encoded < 0) { free(planes); return -1; }
        /* Write plane length as u32 LE */
        uint32_t plen = (uint32_t)encoded;
        out[len_offset]     = (uint8_t)(plen);
        out[len_offset + 1] = (uint8_t)(plen >> 8);
        out[len_offset + 2] = (uint8_t)(plen >> 16);
        out[len_offset + 3] = (uint8_t)(plen >> 24);
        pos += (size_t)encoded;
    }

    free(planes);
    return (int)pos;
}

int glif_compress_planar_rle_decode(const uint8_t *in, size_t len, uint8_t *out, int cells) {
    if (cells <= 0) return 0;
    size_t plane_bytes = (size_t)cells * 4;
    uint8_t *planes = malloc(plane_bytes);
    if (!planes) return -1;

    size_t pos = 0;
    for (int p = 0; p < 4; p++) {
        if (pos + 4 > len) { free(planes); return -1; }
        uint32_t plen = (uint32_t)in[pos] | ((uint32_t)in[pos + 1] << 8) |
                        ((uint32_t)in[pos + 2] << 16) | ((uint32_t)in[pos + 3] << 24);
        pos += 4;
        if (pos + plen > len) { free(planes); return -1; }
        if (glif_compress_byte_rle_decode(in + pos, plen, planes + (size_t)p * cells, cells) != 0) {
            free(planes);
            return -1;
        }
        pos += plen;
    }

    interleave(planes, cells, out);
    free(planes);
    return 0;
}

/* ---- Planar Delta+RLE (P-frame) ---- */

int glif_compress_planar_delta_rle_encode(const uint8_t *cur, const uint8_t *prev,
                                          int cells, uint8_t *out, size_t cap) {
    if (cells <= 0) return 0;
    size_t plane_bytes = (size_t)cells * 4;
    uint8_t *cur_planes = malloc(plane_bytes);
    uint8_t *prev_planes = malloc(plane_bytes);
    uint8_t *xor_plane = malloc((size_t)cells);
    if (!cur_planes || !prev_planes || !xor_plane) {
        free(cur_planes); free(prev_planes); free(xor_plane);
        return -1;
    }

    deinterleave(cur, cells, cur_planes);
    deinterleave(prev, cells, prev_planes);

    size_t pos = 0;
    for (int p = 0; p < 4; p++) {
        const uint8_t *cp = cur_planes + (size_t)p * cells;
        const uint8_t *pp = prev_planes + (size_t)p * cells;
        for (int i = 0; i < cells; i++)
            xor_plane[i] = cp[i] ^ pp[i];

        if (pos + 4 > cap) { free(cur_planes); free(prev_planes); free(xor_plane); return -1; }
        size_t len_offset = pos;
        pos += 4;
        int encoded = glif_compress_byte_rle_encode(xor_plane, cells, out + pos, cap - pos);
        if (encoded < 0) { free(cur_planes); free(prev_planes); free(xor_plane); return -1; }
        uint32_t plen = (uint32_t)encoded;
        out[len_offset]     = (uint8_t)(plen);
        out[len_offset + 1] = (uint8_t)(plen >> 8);
        out[len_offset + 2] = (uint8_t)(plen >> 16);
        out[len_offset + 3] = (uint8_t)(plen >> 24);
        pos += (size_t)encoded;
    }

    free(cur_planes);
    free(prev_planes);
    free(xor_plane);
    return (int)pos;
}

int glif_compress_planar_delta_rle_decode(const uint8_t *in, size_t len,
                                          const uint8_t *prev, uint8_t *out, int cells) {
    if (cells <= 0) return 0;
    size_t plane_bytes = (size_t)cells * 4;
    uint8_t *xor_planes = malloc(plane_bytes);
    uint8_t *prev_planes = malloc(plane_bytes);
    uint8_t *result_planes = malloc(plane_bytes);
    if (!xor_planes || !prev_planes || !result_planes) {
        free(xor_planes); free(prev_planes); free(result_planes);
        return -1;
    }

    deinterleave(prev, cells, prev_planes);

    size_t pos = 0;
    for (int p = 0; p < 4; p++) {
        if (pos + 4 > len) { free(xor_planes); free(prev_planes); free(result_planes); return -1; }
        uint32_t plen = (uint32_t)in[pos] | ((uint32_t)in[pos + 1] << 8) |
                        ((uint32_t)in[pos + 2] << 16) | ((uint32_t)in[pos + 3] << 24);
        pos += 4;
        if (pos + plen > len) { free(xor_planes); free(prev_planes); free(result_planes); return -1; }
        uint8_t *xp = xor_planes + (size_t)p * cells;
        if (glif_compress_byte_rle_decode(in + pos, plen, xp, cells) != 0) {
            free(xor_planes); free(prev_planes); free(result_planes);
            return -1;
        }
        /* XOR with prev plane to reconstruct */
        const uint8_t *pp = prev_planes + (size_t)p * cells;
        uint8_t *rp = result_planes + (size_t)p * cells;
        for (int i = 0; i < cells; i++)
            rp[i] = xp[i] ^ pp[i];
        pos += plen;
    }

    interleave(result_planes, cells, out);
    free(xor_planes);
    free(prev_planes);
    free(result_planes);
    return 0;
}

/* ---- Block Delta ---- */

size_t glif_compress_block_delta_bound(int cols, int rows) {
    int bx = (cols + GLIF_BLOCK_SIZE - 1) / GLIF_BLOCK_SIZE;
    int by = (rows + GLIF_BLOCK_SIZE - 1) / GLIF_BLOCK_SIZE;
    int num_blocks = bx * by;
    size_t bitmap_bytes = ((size_t)num_blocks + 7) / 8;
    /* Worst case: every block changed.
     * Per block: 4 planes × (2 byte length + worst-case byte RLE for up to 64 bytes) */
    int max_block_cells = GLIF_BLOCK_SIZE * GLIF_BLOCK_SIZE;
    size_t per_block = 4 * (2 + glif_compress_byte_rle_bound(max_block_cells));
    return bitmap_bytes + (size_t)num_blocks * per_block;
}

int glif_compress_block_delta_encode(const uint8_t *cur, const uint8_t *prev,
                                     int cols, int rows, uint8_t *out, size_t cap) {
    int bx = (cols + GLIF_BLOCK_SIZE - 1) / GLIF_BLOCK_SIZE;
    int by = (rows + GLIF_BLOCK_SIZE - 1) / GLIF_BLOCK_SIZE;
    int num_blocks = bx * by;
    size_t bitmap_bytes = ((size_t)num_blocks + 7) / 8;

    if (bitmap_bytes > cap) return -1;

    /* Clear bitmap */
    memset(out, 0, bitmap_bytes);

    /* First pass: determine which blocks changed */
    for (int block_y = 0; block_y < by; block_y++) {
        for (int block_x = 0; block_x < bx; block_x++) {
            int block_idx = block_y * bx + block_x;
            int r0 = block_y * GLIF_BLOCK_SIZE;
            int c0 = block_x * GLIF_BLOCK_SIZE;
            int r1 = r0 + GLIF_BLOCK_SIZE; if (r1 > rows) r1 = rows;
            int c1 = c0 + GLIF_BLOCK_SIZE; if (c1 > cols) c1 = cols;

            int changed = 0;
            for (int r = r0; r < r1 && !changed; r++) {
                for (int c = c0; c < c1 && !changed; c++) {
                    int idx = r * cols + c;
                    if (memcmp(cur + idx * 4, prev + idx * 4, 4) != 0)
                        changed = 1;
                }
            }
            if (changed)
                out[block_idx / 8] |= (uint8_t)(1 << (block_idx % 8));
        }
    }

    size_t pos = bitmap_bytes;

    /* Scratch buffers for block extraction */
    int max_block_cells = GLIF_BLOCK_SIZE * GLIF_BLOCK_SIZE;
    uint8_t *block_cur = malloc((size_t)max_block_cells * 4);
    uint8_t *block_prev = malloc((size_t)max_block_cells * 4);
    uint8_t *cur_planes = malloc((size_t)max_block_cells * 4);
    uint8_t *prev_planes = malloc((size_t)max_block_cells * 4);
    uint8_t *xor_plane = malloc((size_t)max_block_cells);
    if (!block_cur || !block_prev || !cur_planes || !prev_planes || !xor_plane) {
        free(block_cur); free(block_prev); free(cur_planes); free(prev_planes); free(xor_plane);
        return -1;
    }

    /* Second pass: encode changed blocks */
    for (int block_y = 0; block_y < by; block_y++) {
        for (int block_x = 0; block_x < bx; block_x++) {
            int block_idx = block_y * bx + block_x;
            if (!(out[block_idx / 8] & (1 << (block_idx % 8))))
                continue;

            int r0 = block_y * GLIF_BLOCK_SIZE;
            int c0 = block_x * GLIF_BLOCK_SIZE;
            int r1 = r0 + GLIF_BLOCK_SIZE; if (r1 > rows) r1 = rows;
            int c1 = c0 + GLIF_BLOCK_SIZE; if (c1 > cols) c1 = cols;
            int bw = c1 - c0;
            int bh = r1 - r0;
            int block_cells = bw * bh;

            /* Extract block cells into contiguous buffer */
            int bi = 0;
            for (int r = r0; r < r1; r++) {
                for (int c = c0; c < c1; c++) {
                    int idx = r * cols + c;
                    memcpy(block_cur + bi * 4, cur + idx * 4, 4);
                    memcpy(block_prev + bi * 4, prev + idx * 4, 4);
                    bi++;
                }
            }

            deinterleave(block_cur, block_cells, cur_planes);
            deinterleave(block_prev, block_cells, prev_planes);

            /* Encode 4 planes with XOR + byte RLE, u16 lengths */
            for (int p = 0; p < 4; p++) {
                const uint8_t *cp = cur_planes + (size_t)p * block_cells;
                const uint8_t *pp = prev_planes + (size_t)p * block_cells;
                for (int i = 0; i < block_cells; i++)
                    xor_plane[i] = cp[i] ^ pp[i];

                if (pos + 2 > cap) {
                    free(block_cur); free(block_prev); free(cur_planes);
                    free(prev_planes); free(xor_plane);
                    return -1;
                }
                size_t len_offset = pos;
                pos += 2;
                int encoded = glif_compress_byte_rle_encode(xor_plane, block_cells,
                                                            out + pos, cap - pos);
                if (encoded < 0 || encoded > 65535) {
                    free(block_cur); free(block_prev); free(cur_planes);
                    free(prev_planes); free(xor_plane);
                    return -1;
                }
                uint16_t plen = (uint16_t)encoded;
                out[len_offset]     = (uint8_t)(plen & 0xFF);
                out[len_offset + 1] = (uint8_t)(plen >> 8);
                pos += (size_t)encoded;
            }
        }
    }

    free(block_cur);
    free(block_prev);
    free(cur_planes);
    free(prev_planes);
    free(xor_plane);
    return (int)pos;
}

int glif_compress_block_delta_decode(const uint8_t *in, size_t len,
                                     const uint8_t *prev, uint8_t *out,
                                     int cols, int rows) {
    /* Start with prev frame */
    memcpy(out, prev, (size_t)(cols * rows) * 4);

    int bx = (cols + GLIF_BLOCK_SIZE - 1) / GLIF_BLOCK_SIZE;
    int by = (rows + GLIF_BLOCK_SIZE - 1) / GLIF_BLOCK_SIZE;
    int num_blocks = bx * by;
    size_t bitmap_bytes = ((size_t)num_blocks + 7) / 8;

    if (bitmap_bytes > len) return -1;

    int max_block_cells = GLIF_BLOCK_SIZE * GLIF_BLOCK_SIZE;
    uint8_t *xor_planes = malloc((size_t)max_block_cells * 4);
    uint8_t *prev_planes = malloc((size_t)max_block_cells * 4);
    uint8_t *result_planes = malloc((size_t)max_block_cells * 4);
    uint8_t *block_buf = malloc((size_t)max_block_cells * 4);
    uint8_t *block_prev = malloc((size_t)max_block_cells * 4);
    if (!xor_planes || !prev_planes || !result_planes || !block_buf || !block_prev) {
        free(xor_planes); free(prev_planes); free(result_planes);
        free(block_buf); free(block_prev);
        return -1;
    }

    size_t pos = bitmap_bytes;

    for (int block_y = 0; block_y < by; block_y++) {
        for (int block_x = 0; block_x < bx; block_x++) {
            int block_idx = block_y * bx + block_x;
            if (!(in[block_idx / 8] & (1 << (block_idx % 8))))
                continue;

            int r0 = block_y * GLIF_BLOCK_SIZE;
            int c0 = block_x * GLIF_BLOCK_SIZE;
            int r1 = r0 + GLIF_BLOCK_SIZE; if (r1 > rows) r1 = rows;
            int c1 = c0 + GLIF_BLOCK_SIZE; if (c1 > cols) c1 = cols;
            int bw = c1 - c0;
            int bh = r1 - r0;
            int block_cells = bw * bh;

            /* Extract prev block cells */
            int bi = 0;
            for (int r = r0; r < r1; r++) {
                for (int c = c0; c < c1; c++) {
                    int idx = r * cols + c;
                    memcpy(block_prev + bi * 4, prev + idx * 4, 4);
                    bi++;
                }
            }
            deinterleave(block_prev, block_cells, prev_planes);

            /* Decode 4 planes */
            for (int p = 0; p < 4; p++) {
                if (pos + 2 > len) {
                    free(xor_planes); free(prev_planes); free(result_planes);
                    free(block_buf); free(block_prev);
                    return -1;
                }
                uint16_t plen = (uint16_t)(in[pos] | (in[pos + 1] << 8));
                pos += 2;
                if (pos + plen > len) {
                    free(xor_planes); free(prev_planes); free(result_planes);
                    free(block_buf); free(block_prev);
                    return -1;
                }
                uint8_t *xp = xor_planes + (size_t)p * block_cells;
                if (glif_compress_byte_rle_decode(in + pos, plen, xp, block_cells) != 0) {
                    free(xor_planes); free(prev_planes); free(result_planes);
                    free(block_buf); free(block_prev);
                    return -1;
                }
                /* XOR with prev plane */
                const uint8_t *pp = prev_planes + (size_t)p * block_cells;
                uint8_t *rp = result_planes + (size_t)p * block_cells;
                for (int i = 0; i < block_cells; i++)
                    rp[i] = xp[i] ^ pp[i];
                pos += plen;
            }

            /* Interleave and scatter back into output */
            interleave(result_planes, block_cells, block_buf);
            bi = 0;
            for (int r = r0; r < r1; r++) {
                for (int c = c0; c < c1; c++) {
                    int idx = r * cols + c;
                    memcpy(out + idx * 4, block_buf + bi * 4, 4);
                    bi++;
                }
            }
        }
    }

    free(xor_planes);
    free(prev_planes);
    free(result_planes);
    free(block_buf);
    free(block_prev);
    return 0;
}

/* ---- Deflate (zlib via miniz) ---- */

size_t glif_compress_deflate_bound(int cells) {
    mz_ulong src_len = (mz_ulong)cells * 4;
    return (size_t)mz_compressBound(src_len);
}

/* Deflate keyframe: deinterleave → compress planar data */
int glif_compress_deflate_encode(const uint8_t *cur, int cells, uint8_t *out, size_t cap) {
    if (cells <= 0) return 0;
    size_t plane_bytes = (size_t)cells * 4;
    uint8_t *planes = malloc(plane_bytes);
    if (!planes) return -1;

    deinterleave(cur, cells, planes);

    mz_ulong dest_len = (mz_ulong)cap;
    int ret = mz_compress2(out, &dest_len, planes, (mz_ulong)plane_bytes, MZ_DEFAULT_COMPRESSION);
    free(planes);
    if (ret != MZ_OK) return -1;
    return (int)dest_len;
}

/* Deflate keyframe decode: decompress → interleave */
int glif_compress_deflate_decode(const uint8_t *in, size_t len, uint8_t *out, int cells) {
    if (cells <= 0) return 0;
    size_t plane_bytes = (size_t)cells * 4;
    uint8_t *planes = malloc(plane_bytes);
    if (!planes) return -1;

    mz_ulong dest_len = (mz_ulong)plane_bytes;
    int ret = mz_uncompress(planes, &dest_len, in, (mz_ulong)len);
    if (ret != MZ_OK || dest_len != (mz_ulong)plane_bytes) {
        free(planes);
        return -1;
    }

    interleave(planes, cells, out);
    free(planes);
    return 0;
}

/* Delta+deflate P-frame: deinterleave both → XOR planes → compress */
int glif_compress_delta_deflate_encode(const uint8_t *cur, const uint8_t *prev,
                                       int cells, uint8_t *work, uint8_t *out, size_t cap) {
    if (cells <= 0) return 0;
    size_t plane_bytes = (size_t)cells * 4;
    /* work buffer used for XOR'd planar data */
    uint8_t *cur_planes = malloc(plane_bytes);
    uint8_t *prev_planes = malloc(plane_bytes);
    if (!cur_planes || !prev_planes) {
        free(cur_planes); free(prev_planes);
        return -1;
    }

    deinterleave(cur, cells, cur_planes);
    deinterleave(prev, cells, prev_planes);

    /* XOR into work buffer (reuse caller's work buf for planar XOR) */
    for (size_t i = 0; i < plane_bytes; i++)
        work[i] = cur_planes[i] ^ prev_planes[i];

    free(cur_planes);
    free(prev_planes);

    mz_ulong dest_len = (mz_ulong)cap;
    int ret = mz_compress2(out, &dest_len, work, (mz_ulong)plane_bytes, MZ_DEFAULT_COMPRESSION);
    if (ret != MZ_OK) return -1;
    return (int)dest_len;
}

/* Delta+deflate decode: decompress → XOR with prev planes → interleave */
int glif_compress_delta_deflate_decode(const uint8_t *in, size_t len,
                                       const uint8_t *prev, uint8_t *work,
                                       uint8_t *out, int cells) {
    if (cells <= 0) return 0;
    size_t plane_bytes = (size_t)cells * 4;

    /* Decompress XOR'd planar data into work */
    mz_ulong dest_len = (mz_ulong)plane_bytes;
    int ret = mz_uncompress(work, &dest_len, in, (mz_ulong)len);
    if (ret != MZ_OK || dest_len != (mz_ulong)plane_bytes) return -1;

    /* Deinterleave prev to get prev planes */
    uint8_t *prev_planes = malloc(plane_bytes);
    uint8_t *result_planes = malloc(plane_bytes);
    if (!prev_planes || !result_planes) {
        free(prev_planes); free(result_planes);
        return -1;
    }

    deinterleave(prev, cells, prev_planes);

    /* XOR to reconstruct planes */
    for (size_t i = 0; i < plane_bytes; i++)
        result_planes[i] = work[i] ^ prev_planes[i];

    interleave(result_planes, cells, out);
    free(prev_planes);
    free(result_planes);
    return 0;
}
