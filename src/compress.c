#include "compress.h"
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
