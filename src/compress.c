#include "compress.h"
#include "miniz.h"
#include <stdlib.h>
#include <string.h>

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

/* ---- Deflate (zlib via miniz) ---- */

size_t glif_compress_deflate_bound(int cells) {
    mz_ulong src_len = (mz_ulong)cells * 4;
    return (size_t)mz_compressBound(src_len);
}

/* Deflate keyframe: deinterleave -> compress planar data */
int glif_compress_deflate_encode(const uint8_t *cur, int cells, uint8_t *out, size_t cap) {
    if (cells <= 0) return 0;
    size_t plane_bytes = (size_t)cells * 4;
    uint8_t *planes = malloc(plane_bytes);
    if (!planes) return -1;

    deinterleave(cur, cells, planes);

    mz_ulong dest_len = (mz_ulong)cap;
    int ret = mz_compress2(out, &dest_len, planes, (mz_ulong)plane_bytes, MZ_BEST_COMPRESSION);
    free(planes);
    if (ret != MZ_OK) return -1;
    return (int)dest_len;
}

/* Deflate keyframe decode: decompress -> interleave */
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

/* Delta+deflate P-frame: deinterleave both -> XOR planes -> compress */
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
    int ret = mz_compress2(out, &dest_len, work, (mz_ulong)plane_bytes, MZ_BEST_COMPRESSION);
    if (ret != MZ_OK) return -1;
    return (int)dest_len;
}

/* Delta+deflate decode: decompress -> XOR with prev planes -> interleave */
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

/* ---- PNG-style row prediction filters ---- */

static uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = p - (int)a; if (pa < 0) pa = -pa;
    int pb = p - (int)b; if (pb < 0) pb = -pb;
    int pc = p - (int)c; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

/* Apply adaptive row filter to a single plane (rows x cols).
 * Output: rows x (1 + cols) bytes -- one filter-type byte per row. */
static void filter_plane_rows(const uint8_t *plane, int cols, int rows,
                               uint8_t *filtered) {
    size_t out_pos = 0;
    for (int r = 0; r < rows; r++) {
        const uint8_t *row = plane + r * cols;
        const uint8_t *above = (r > 0) ? plane + (r - 1) * cols : NULL;

        /* Try all 5 PNG filter types, pick minimum absolute-value sum */
        int best_filter = 0;
        int best_sum = 0x7FFFFFFF;
        for (int ft = 0; ft < 5; ft++) {
            int sum = 0;
            for (int c = 0; c < cols; c++) {
                uint8_t raw = row[c];
                uint8_t left = (c > 0) ? row[c - 1] : 0;
                uint8_t up = above ? above[c] : 0;
                uint8_t up_left = (c > 0 && above) ? above[c - 1] : 0;
                uint8_t pred;
                switch (ft) {
                case 1: pred = left; break;
                case 2: pred = up; break;
                case 3: pred = (uint8_t)(((int)left + (int)up) / 2); break;
                case 4: pred = paeth_predictor(left, up, up_left); break;
                default: pred = 0; break;
                }
                int v = (int)(uint8_t)(raw - pred);
                sum += (v < 128) ? v : 256 - v;
            }
            if (sum < best_sum) {
                best_sum = sum;
                best_filter = ft;
            }
        }

        filtered[out_pos++] = (uint8_t)best_filter;
        for (int c = 0; c < cols; c++) {
            uint8_t raw = row[c];
            uint8_t left = (c > 0) ? row[c - 1] : 0;
            uint8_t up = above ? above[c] : 0;
            uint8_t up_left = (c > 0 && above) ? above[c - 1] : 0;
            uint8_t pred;
            switch (best_filter) {
            case 1: pred = left; break;
            case 2: pred = up; break;
            case 3: pred = (uint8_t)(((int)left + (int)up) / 2); break;
            case 4: pred = paeth_predictor(left, up, up_left); break;
            default: pred = 0; break;
            }
            filtered[out_pos++] = raw - pred;
        }
    }
}

/* Reverse row filter to reconstruct plane data */
static void unfilter_plane_rows(const uint8_t *filtered, int cols, int rows,
                                 uint8_t *plane) {
    size_t in_pos = 0;
    for (int r = 0; r < rows; r++) {
        uint8_t *row = plane + r * cols;
        const uint8_t *above = (r > 0) ? plane + (r - 1) * cols : NULL;
        uint8_t ft = filtered[in_pos++];
        for (int c = 0; c < cols; c++) {
            uint8_t delta_val = filtered[in_pos++];
            uint8_t left = (c > 0) ? row[c - 1] : 0;
            uint8_t up = above ? above[c] : 0;
            uint8_t up_left = (c > 0 && above) ? above[c - 1] : 0;
            uint8_t pred;
            switch (ft) {
            case 1: pred = left; break;
            case 2: pred = up; break;
            case 3: pred = (uint8_t)(((int)left + (int)up) / 2); break;
            case 4: pred = paeth_predictor(left, up, up_left); break;
            default: pred = 0; break;
            }
            row[c] = delta_val + pred;
        }
    }
}

/* ---- Filtered deflate ---- */

size_t glif_compress_filtered_deflate_bound(int cols, int rows) {
    mz_ulong src_len = (mz_ulong)(4 * rows) * (mz_ulong)(1 + cols);
    return (size_t)mz_compressBound(src_len);
}

int glif_compress_filtered_deflate_encode(const uint8_t *cur, int cols, int rows,
                                           uint8_t *out, size_t cap) {
    int cells = cols * rows;
    if (cells <= 0) return 0;

    size_t plane_bytes = (size_t)cells * 4;
    size_t filt_plane = (size_t)rows * ((size_t)cols + 1);
    size_t total_filt = filt_plane * 4;

    uint8_t *planes = malloc(plane_bytes);
    uint8_t *filtered = malloc(total_filt);
    if (!planes || !filtered) { free(planes); free(filtered); return -1; }

    deinterleave(cur, cells, planes);
    for (int p = 0; p < 4; p++)
        filter_plane_rows(planes + (size_t)p * cells, cols, rows,
                          filtered + (size_t)p * filt_plane);

    mz_ulong dest_len = (mz_ulong)cap;
    int ret = mz_compress2(out, &dest_len, filtered, (mz_ulong)total_filt,
                           MZ_BEST_COMPRESSION);
    free(planes); free(filtered);
    if (ret != MZ_OK) return -1;
    return (int)dest_len;
}

int glif_compress_filtered_deflate_decode(const uint8_t *in, size_t len,
                                           uint8_t *out, int cols, int rows) {
    int cells = cols * rows;
    if (cells <= 0) return 0;

    size_t filt_plane = (size_t)rows * ((size_t)cols + 1);
    size_t total_filt = filt_plane * 4;
    size_t plane_bytes = (size_t)cells * 4;

    uint8_t *filtered = malloc(total_filt);
    uint8_t *planes = malloc(plane_bytes);
    if (!filtered || !planes) { free(filtered); free(planes); return -1; }

    mz_ulong dest_len = (mz_ulong)total_filt;
    int ret = mz_uncompress(filtered, &dest_len, in, (mz_ulong)len);
    if (ret != MZ_OK || dest_len != (mz_ulong)total_filt) {
        free(filtered); free(planes); return -1;
    }

    for (int p = 0; p < 4; p++)
        unfilter_plane_rows(filtered + (size_t)p * filt_plane, cols, rows,
                            planes + (size_t)p * cells);

    interleave(planes, cells, out);
    free(filtered); free(planes);
    return 0;
}

int glif_compress_delta_filtered_deflate_encode(const uint8_t *cur, const uint8_t *prev,
                                                 int cols, int rows,
                                                 uint8_t *out, size_t cap) {
    int cells = cols * rows;
    if (cells <= 0) return 0;

    size_t plane_bytes = (size_t)cells * 4;
    size_t filt_plane = (size_t)rows * ((size_t)cols + 1);
    size_t total_filt = filt_plane * 4;

    uint8_t *cur_planes = malloc(plane_bytes);
    uint8_t *prev_planes = malloc(plane_bytes);
    uint8_t *xor_planes = malloc(plane_bytes);
    uint8_t *filtered = malloc(total_filt);
    if (!cur_planes || !prev_planes || !xor_planes || !filtered) {
        free(cur_planes); free(prev_planes); free(xor_planes); free(filtered);
        return -1;
    }

    deinterleave(cur, cells, cur_planes);
    deinterleave(prev, cells, prev_planes);
    for (size_t i = 0; i < plane_bytes; i++)
        xor_planes[i] = cur_planes[i] ^ prev_planes[i];

    for (int p = 0; p < 4; p++)
        filter_plane_rows(xor_planes + (size_t)p * cells, cols, rows,
                          filtered + (size_t)p * filt_plane);

    mz_ulong dest_len = (mz_ulong)cap;
    int ret = mz_compress2(out, &dest_len, filtered, (mz_ulong)total_filt,
                           MZ_BEST_COMPRESSION);
    free(cur_planes); free(prev_planes); free(xor_planes); free(filtered);
    if (ret != MZ_OK) return -1;
    return (int)dest_len;
}

int glif_compress_delta_filtered_deflate_decode(const uint8_t *in, size_t len,
                                                 const uint8_t *prev,
                                                 uint8_t *out, int cols, int rows) {
    int cells = cols * rows;
    if (cells <= 0) return 0;

    size_t filt_plane = (size_t)rows * ((size_t)cols + 1);
    size_t total_filt = filt_plane * 4;
    size_t plane_bytes = (size_t)cells * 4;

    uint8_t *filtered = malloc(total_filt);
    uint8_t *xor_planes = malloc(plane_bytes);
    uint8_t *prev_planes = malloc(plane_bytes);
    uint8_t *result_planes = malloc(plane_bytes);
    if (!filtered || !xor_planes || !prev_planes || !result_planes) {
        free(filtered); free(xor_planes); free(prev_planes); free(result_planes);
        return -1;
    }

    mz_ulong dest_len = (mz_ulong)total_filt;
    int ret = mz_uncompress(filtered, &dest_len, in, (mz_ulong)len);
    if (ret != MZ_OK || dest_len != (mz_ulong)total_filt) {
        free(filtered); free(xor_planes); free(prev_planes); free(result_planes);
        return -1;
    }

    for (int p = 0; p < 4; p++)
        unfilter_plane_rows(filtered + (size_t)p * filt_plane, cols, rows,
                            xor_planes + (size_t)p * cells);

    deinterleave(prev, cells, prev_planes);
    for (size_t i = 0; i < plane_bytes; i++)
        result_planes[i] = xor_planes[i] ^ prev_planes[i];

    interleave(result_planes, cells, out);
    free(filtered); free(xor_planes); free(prev_planes); free(result_planes);
    return 0;
}

/* ---- Palette deflate ---- */

size_t glif_compress_palette_deflate_bound(int cells) {
    /* Pre-deflate max: 1 (count) + 256*3 (palette) + cells*2 (chars+indices) */
    mz_ulong src_len = (mz_ulong)(1 + 256 * 3 + cells * 2);
    return (size_t)mz_compressBound(src_len);
}

int glif_compress_palette_deflate_encode(const uint8_t *cur, int cells,
                                          uint8_t *out, size_t cap) {
    if (cells <= 0) return 0;

    /* Find unique (r,g,b) triples -- bail if > 256 */
    uint8_t palette[256 * 3];
    int num_colors = 0;
    uint8_t *indices = malloc((size_t)cells);
    if (!indices) return -1;

    for (int i = 0; i < cells; i++) {
        uint8_t r = cur[i * 4 + 1];
        uint8_t g = cur[i * 4 + 2];
        uint8_t b = cur[i * 4 + 3];
        int found = -1;
        for (int j = 0; j < num_colors; j++) {
            if (palette[j * 3] == r && palette[j * 3 + 1] == g && palette[j * 3 + 2] == b) {
                found = j;
                break;
            }
        }
        if (found >= 0) {
            indices[i] = (uint8_t)found;
        } else if (num_colors < 256) {
            palette[num_colors * 3]     = r;
            palette[num_colors * 3 + 1] = g;
            palette[num_colors * 3 + 2] = b;
            indices[i] = (uint8_t)num_colors;
            num_colors++;
        } else {
            free(indices);
            return -1;  /* > 256 colors, skip this codec */
        }
    }

    /* Build pre-deflate: [num_colors_minus_1: u8] [palette: N*3] [chars: cells] [indices: cells] */
    size_t pre_len = 1 + (size_t)num_colors * 3 + (size_t)cells * 2;
    uint8_t *pre = malloc(pre_len);
    if (!pre) { free(indices); return -1; }

    pre[0] = (uint8_t)(num_colors - 1);  /* 0 = 1 color, 255 = 256 colors */
    memcpy(pre + 1, palette, (size_t)num_colors * 3);

    size_t chars_off = 1 + (size_t)num_colors * 3;
    for (int i = 0; i < cells; i++)
        pre[chars_off + i] = cur[i * 4];
    memcpy(pre + chars_off + cells, indices, (size_t)cells);
    free(indices);

    mz_ulong dest_len = (mz_ulong)cap;
    int ret = mz_compress2(out, &dest_len, pre, (mz_ulong)pre_len, MZ_BEST_COMPRESSION);
    free(pre);
    if (ret != MZ_OK) return -1;
    return (int)dest_len;
}

int glif_compress_palette_deflate_decode(const uint8_t *in, size_t len,
                                          uint8_t *out, int cells) {
    if (cells <= 0) return 0;

    /* Max pre-deflate size: 1 + 256*3 + cells*2 */
    size_t max_pre = 1 + 256 * 3 + (size_t)cells * 2;
    uint8_t *pre = malloc(max_pre);
    if (!pre) return -1;

    mz_ulong dest_len = (mz_ulong)max_pre;
    int ret = mz_uncompress(pre, &dest_len, in, (mz_ulong)len);
    if (ret != MZ_OK) { free(pre); return -1; }

    int num_colors = (int)pre[0] + 1;
    size_t expected = 1 + (size_t)num_colors * 3 + (size_t)cells * 2;
    if ((size_t)dest_len != expected) { free(pre); return -1; }

    const uint8_t *pal = pre + 1;
    const uint8_t *chars = pre + 1 + (size_t)num_colors * 3;
    const uint8_t *idx = chars + cells;

    for (int i = 0; i < cells; i++) {
        uint8_t ci = idx[i];
        if (ci >= num_colors) { free(pre); return -1; }
        out[i * 4]     = chars[i];
        out[i * 4 + 1] = pal[ci * 3];
        out[i * 4 + 2] = pal[ci * 3 + 1];
        out[i * 4 + 3] = pal[ci * 3 + 2];
    }

    free(pre);
    return 0;
}

/* ---- Per-plane separate deflate ---- */

size_t glif_compress_planar_deflate_bound(int cells) {
    /* 4 planes, each: u32 length + deflated data */
    return 4 * (4 + (size_t)mz_compressBound((mz_ulong)cells));
}

int glif_compress_planar_deflate_encode(const uint8_t *cur, int cells,
                                         uint8_t *out, size_t cap) {
    if (cells <= 0) return 0;
    size_t plane_bytes = (size_t)cells * 4;
    uint8_t *planes = malloc(plane_bytes);
    if (!planes) return -1;

    deinterleave(cur, cells, planes);

    size_t pos = 0;
    for (int p = 0; p < 4; p++) {
        if (pos + 4 > cap) { free(planes); return -1; }
        mz_ulong dest_len = (mz_ulong)(cap - pos - 4);
        int ret = mz_compress2(out + pos + 4, &dest_len,
                               planes + (size_t)p * cells, (mz_ulong)cells,
                               MZ_BEST_COMPRESSION);
        if (ret != MZ_OK) { free(planes); return -1; }

        uint32_t clen = (uint32_t)dest_len;
        out[pos]     = (uint8_t)(clen);
        out[pos + 1] = (uint8_t)(clen >> 8);
        out[pos + 2] = (uint8_t)(clen >> 16);
        out[pos + 3] = (uint8_t)(clen >> 24);
        pos += 4 + (size_t)clen;
    }

    free(planes);
    return (int)pos;
}

int glif_compress_planar_deflate_decode(const uint8_t *in, size_t len,
                                         uint8_t *out, int cells) {
    if (cells <= 0) return 0;
    size_t plane_bytes = (size_t)cells * 4;
    uint8_t *planes = malloc(plane_bytes);
    if (!planes) return -1;

    size_t pos = 0;
    for (int p = 0; p < 4; p++) {
        if (pos + 4 > len) { free(planes); return -1; }
        uint32_t clen = (uint32_t)in[pos] | ((uint32_t)in[pos + 1] << 8) |
                        ((uint32_t)in[pos + 2] << 16) | ((uint32_t)in[pos + 3] << 24);
        pos += 4;
        if (pos + clen > len) { free(planes); return -1; }

        mz_ulong dest_len = (mz_ulong)cells;
        int ret = mz_uncompress(planes + (size_t)p * cells, &dest_len,
                                in + pos, (mz_ulong)clen);
        if (ret != MZ_OK || dest_len != (mz_ulong)cells) { free(planes); return -1; }
        pos += clen;
    }

    interleave(planes, cells, out);
    free(planes);
    return 0;
}

int glif_compress_delta_planar_deflate_encode(const uint8_t *cur, const uint8_t *prev,
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

        if (pos + 4 > cap) {
            free(cur_planes); free(prev_planes); free(xor_plane);
            return -1;
        }
        mz_ulong dest_len = (mz_ulong)(cap - pos - 4);
        int ret = mz_compress2(out + pos + 4, &dest_len,
                               xor_plane, (mz_ulong)cells,
                               MZ_BEST_COMPRESSION);
        if (ret != MZ_OK) {
            free(cur_planes); free(prev_planes); free(xor_plane);
            return -1;
        }

        uint32_t clen = (uint32_t)dest_len;
        out[pos]     = (uint8_t)(clen);
        out[pos + 1] = (uint8_t)(clen >> 8);
        out[pos + 2] = (uint8_t)(clen >> 16);
        out[pos + 3] = (uint8_t)(clen >> 24);
        pos += 4 + (size_t)clen;
    }

    free(cur_planes); free(prev_planes); free(xor_plane);
    return (int)pos;
}

int glif_compress_delta_planar_deflate_decode(const uint8_t *in, size_t len,
                                               const uint8_t *prev,
                                               uint8_t *out, int cells) {
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
        if (pos + 4 > len) {
            free(xor_planes); free(prev_planes); free(result_planes);
            return -1;
        }
        uint32_t clen = (uint32_t)in[pos] | ((uint32_t)in[pos + 1] << 8) |
                        ((uint32_t)in[pos + 2] << 16) | ((uint32_t)in[pos + 3] << 24);
        pos += 4;
        if (pos + clen > len) {
            free(xor_planes); free(prev_planes); free(result_planes);
            return -1;
        }

        mz_ulong dest_len = (mz_ulong)cells;
        int ret = mz_uncompress(xor_planes + (size_t)p * cells, &dest_len,
                                in + pos, (mz_ulong)clen);
        if (ret != MZ_OK || dest_len != (mz_ulong)cells) {
            free(xor_planes); free(prev_planes); free(result_planes);
            return -1;
        }

        const uint8_t *pp = prev_planes + (size_t)p * cells;
        uint8_t *xp = xor_planes + (size_t)p * cells;
        uint8_t *rp = result_planes + (size_t)p * cells;
        for (int i = 0; i < cells; i++)
            rp[i] = xp[i] ^ pp[i];
        pos += clen;
    }

    interleave(result_planes, cells, out);
    free(xor_planes); free(prev_planes); free(result_planes);
    return 0;
}
