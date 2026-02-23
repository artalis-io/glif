/* Analyze per-frame codec choices and compression on a .glif file */
#include "glif.h"
#include "output.h"
#include "compress.h"
#include "miniz.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.glif> [max_frames]\n", argv[0]);
        return 1;
    }
#if defined(__OpenBSD__) || defined(__linux__)
    #include "sandbox.h"
    glif_unveil(argv[1], "r");
    glif_unveil_lock();
    if (glif_pledge("stdio rpath") != 0)
        fprintf(stderr, "warning: pledge() failed\n");
#endif

    int max_analyze = 200;
    if (argc >= 3) {
        char *endptr;
        long val = strtol(argv[2], &endptr, 10);
        if (*endptr != '\0' || val <= 0 || val > INT_MAX) {
            fprintf(stderr, "error: invalid max_frames\n");
            return 1;
        }
        max_analyze = (int)val;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "error: cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    if (flen < 0) {
        fprintf(stderr, "error: ftell failed\n");
        fclose(f);
        return 1;
    }
    rewind(f);
    uint8_t *data = malloc((size_t)flen);
    if (!data || fread(data, 1, (size_t)flen, f) != (size_t)flen) {
        fprintf(stderr, "error: read failed\n"); fclose(f); return 1;
    }
    fclose(f);

    GlifReader gr;
    if (glif_reader_open(&gr, data, (size_t)flen) != 0) {
        fprintf(stderr, "error: invalid glif file\n"); free(data); return 1;
    }

    const GlifHeader *hdr = glif_reader_header(&gr);
    int total = hdr->cols * hdr->rows;
    int raw_size = total * 4;
    fprintf(stderr, "Grid: %dx%d (%d cells), raw frame: %d bytes\n",
            hdr->cols, hdr->rows, total, raw_size);

    uint8_t *prev = calloc((size_t)total * 4, 1);
    uint8_t *work = malloc((size_t)total * 4);
    uint8_t *planes = malloc((size_t)total * 4);
    uint8_t *prev_planes = malloc((size_t)total * 4);
    size_t enc_cap = glif_compress_deflate_bound(total);
    uint8_t *enc = malloc(enc_cap);
    if (!prev || !work || !planes || !prev_planes || !enc) {
        fprintf(stderr, "error: allocation failed\n");
        free(prev); free(work); free(planes); free(prev_planes); free(enc);
        glif_reader_close(&gr);
        free(data);
        return 1;
    }

    int analyze = (int)hdr->frames < max_analyze ? (int)hdr->frames : max_analyze;

    /* Accumulators */
    long long total_raw = 0;
    long long total_deflate_interleaved = 0;
    long long total_deflate_planar = 0;
    long long total_delta_deflate_interleaved = 0;
    long long total_delta_deflate_planar = 0;

    /* Per-channel analysis */
    long long ch_plane_total = 0, r_plane_total = 0, g_plane_total = 0, b_plane_total = 0;
    long long ch_delta_total = 0, r_delta_total = 0, g_delta_total = 0, b_delta_total = 0;

    /* Change stats */
    long long total_changed_cells = 0;
    long long total_unchanged_cells = 0;

    for (int i = 0; i < analyze; i++) {
        if (glif_reader_decode(&gr, (uint32_t)i) != 0) break;
        const uint8_t *cur = glif_reader_frame_data(&gr);
        total_raw += raw_size;

        /* Deflate interleaved (raw) */
        mz_ulong dlen = (mz_ulong)enc_cap;
        mz_compress2(enc, &dlen, cur, (mz_ulong)raw_size, MZ_DEFAULT_COMPRESSION);
        total_deflate_interleaved += (long long)dlen;

        /* Deflate planar */
        /* deinterleave */
        for (int j = 0; j < total; j++) {
            planes[j]           = cur[j * 4];
            planes[total + j]   = cur[j * 4 + 1];
            planes[total*2 + j] = cur[j * 4 + 2];
            planes[total*3 + j] = cur[j * 4 + 3];
        }
        dlen = (mz_ulong)enc_cap;
        mz_compress2(enc, &dlen, planes, (mz_ulong)raw_size, MZ_DEFAULT_COMPRESSION);
        total_deflate_planar += (long long)dlen;

        /* Per-channel deflate sizes */
        dlen = (mz_ulong)enc_cap;
        mz_compress2(enc, &dlen, planes, (mz_ulong)total, MZ_DEFAULT_COMPRESSION);
        ch_plane_total += (long long)dlen;
        dlen = (mz_ulong)enc_cap;
        mz_compress2(enc, &dlen, planes + total, (mz_ulong)total, MZ_DEFAULT_COMPRESSION);
        r_plane_total += (long long)dlen;
        dlen = (mz_ulong)enc_cap;
        mz_compress2(enc, &dlen, planes + total*2, (mz_ulong)total, MZ_DEFAULT_COMPRESSION);
        g_plane_total += (long long)dlen;
        dlen = (mz_ulong)enc_cap;
        mz_compress2(enc, &dlen, planes + total*3, (mz_ulong)total, MZ_DEFAULT_COMPRESSION);
        b_plane_total += (long long)dlen;

        if (i > 0) {
            /* Count changed vs unchanged cells */
            int changed = 0;
            for (int j = 0; j < total; j++) {
                if (memcmp(cur + j*4, prev + j*4, 4) != 0) changed++;
            }
            total_changed_cells += changed;
            total_unchanged_cells += (total - changed);

            /* Delta deflate interleaved */
            for (int j = 0; j < raw_size; j++) work[j] = cur[j] ^ prev[j];
            dlen = (mz_ulong)enc_cap;
            mz_compress2(enc, &dlen, work, (mz_ulong)raw_size, MZ_DEFAULT_COMPRESSION);
            total_delta_deflate_interleaved += (long long)dlen;

            /* Delta deflate planar */
            for (int j = 0; j < total; j++) {
                prev_planes[j]           = prev[j * 4];
                prev_planes[total + j]   = prev[j * 4 + 1];
                prev_planes[total*2 + j] = prev[j * 4 + 2];
                prev_planes[total*3 + j] = prev[j * 4 + 3];
            }
            for (int j = 0; j < raw_size; j++) work[j] = planes[j] ^ prev_planes[j];
            dlen = (mz_ulong)enc_cap;
            mz_compress2(enc, &dlen, work, (mz_ulong)raw_size, MZ_DEFAULT_COMPRESSION);
            total_delta_deflate_planar += (long long)dlen;

            /* Per-channel delta deflate */
            dlen = (mz_ulong)enc_cap;
            for (int j = 0; j < total; j++) work[j] = planes[j] ^ prev_planes[j];
            mz_compress2(enc, &dlen, work, (mz_ulong)total, MZ_DEFAULT_COMPRESSION);
            ch_delta_total += (long long)dlen;
            dlen = (mz_ulong)enc_cap;
            for (int j = 0; j < total; j++) work[j] = planes[total+j] ^ prev_planes[total+j];
            mz_compress2(enc, &dlen, work, (mz_ulong)total, MZ_DEFAULT_COMPRESSION);
            r_delta_total += (long long)dlen;
            dlen = (mz_ulong)enc_cap;
            for (int j = 0; j < total; j++) work[j] = planes[total*2+j] ^ prev_planes[total*2+j];
            mz_compress2(enc, &dlen, work, (mz_ulong)total, MZ_DEFAULT_COMPRESSION);
            g_delta_total += (long long)dlen;
            dlen = (mz_ulong)enc_cap;
            for (int j = 0; j < total; j++) work[j] = planes[total*3+j] ^ prev_planes[total*3+j];
            mz_compress2(enc, &dlen, work, (mz_ulong)total, MZ_DEFAULT_COMPRESSION);
            b_delta_total += (long long)dlen;
        }

        memcpy(prev, cur, (size_t)raw_size);
    }

    fprintf(stderr, "\n=== Analysis of first %d frames ===\n", analyze);
    fprintf(stderr, "RAW total:                 %8lld bytes\n", total_raw);
    fprintf(stderr, "Deflate (interleaved):     %8lld bytes (%.2fx)\n",
            total_deflate_interleaved, (double)total_raw / total_deflate_interleaved);
    fprintf(stderr, "Deflate (planar):          %8lld bytes (%.2fx)\n",
            total_deflate_planar, (double)total_raw / total_deflate_planar);
    fprintf(stderr, "Delta+deflate (interl.):   %8lld bytes (%.2fx)\n",
            total_delta_deflate_interleaved, (double)total_raw / total_delta_deflate_interleaved);
    fprintf(stderr, "Delta+deflate (planar):    %8lld bytes (%.2fx)\n",
            total_delta_deflate_planar, (double)total_raw / total_delta_deflate_planar);

    int delta_frames = analyze - 1;
    if (delta_frames > 0) {
        fprintf(stderr, "\n--- Cell change stats (frames 1-%d) ---\n", analyze - 1);
        fprintf(stderr, "Avg changed cells/frame:   %lld / %d (%.1f%%)\n",
                total_changed_cells / delta_frames, total,
                100.0 * total_changed_cells / (delta_frames * (long long)total));
        fprintf(stderr, "Avg unchanged cells/frame: %lld / %d (%.1f%%)\n",
                total_unchanged_cells / delta_frames, total,
                100.0 * total_unchanged_cells / (delta_frames * (long long)total));
    }

    fprintf(stderr, "\n--- Per-channel keyframe deflate (avg bytes/frame) ---\n");
    fprintf(stderr, "Ch plane:  %.0f bytes\n", (double)ch_plane_total / analyze);
    fprintf(stderr, "R plane:   %.0f bytes\n", (double)r_plane_total / analyze);
    fprintf(stderr, "G plane:   %.0f bytes\n", (double)g_plane_total / analyze);
    fprintf(stderr, "B plane:   %.0f bytes\n", (double)b_plane_total / analyze);

    if (delta_frames > 0) {
        fprintf(stderr, "\n--- Per-channel delta+deflate (avg bytes/frame) ---\n");
        fprintf(stderr, "Ch delta:  %.0f bytes\n", (double)ch_delta_total / delta_frames);
        fprintf(stderr, "R delta:   %.0f bytes\n", (double)r_delta_total / delta_frames);
        fprintf(stderr, "G delta:   %.0f bytes\n", (double)g_delta_total / delta_frames);
        fprintf(stderr, "B delta:   %.0f bytes\n", (double)b_delta_total / delta_frames);
    }

    free(prev); free(work); free(planes); free(prev_planes); free(enc);
    glif_reader_close(&gr);
    free(data);
    return 0;
}
