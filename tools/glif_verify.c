/* Quick diagnostic: decode a .glif file and verify data integrity */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glif.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: glif_verify <file.glif> [dump_frame]\n");
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "Cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    if (flen < 0) { fprintf(stderr, "ftell failed\n"); fclose(f); return 1; }
    rewind(f);
    uint8_t *buf = malloc((size_t)flen);
    if (fread(buf, 1, (size_t)flen, f) != (size_t)flen) {
        fprintf(stderr, "fread failed\n");
        free(buf);
        fclose(f);
        return 1;
    }
    fclose(f);

    GlifReader gr;
    if (glif_reader_open(&gr, buf, (size_t)flen) != 0) {
        fprintf(stderr, "Failed to open .glif\n");
        free(buf);
        return 1;
    }

    const GlifHeader *hdr = &gr.header;
    printf("Version: %d, Flags: 0x%02x\n", hdr->version, hdr->flags);
    printf("Grid: %dx%d (%d cells), Cell: %dx%d, FPS: %.1f, Frames: %u\n",
           hdr->cols, hdr->rows, hdr->cols * hdr->rows,
           hdr->cell_w, hdr->cell_h, hdr->fps, hdr->frames);

    /* Count frame types */
    int type_counts[16] = {0};
    for (uint32_t i = 0; i < hdr->frames; i++) {
        if (gr.index[i].type < 16) type_counts[gr.index[i].type]++;
    }
    printf("Frame types:");
    const char *type_names[] = {
        "DEFLATE", "D_DEFLATE", "FILT_DEFL", "D_FILT_DEFL",
        "PAL_DEFL", NULL, "PL_DEFL", "D_PL_DEFL"
    };
    for (int t = 0; t < 8; t++) {
        if (type_counts[t] > 0 && type_names[t])
            printf(" %s=%d", type_names[t], type_counts[t]);
    }
    printf("\n");

    /* Decode all frames sequentially, check for errors */
    int errors = 0;
    int cells = hdr->cols * hdr->rows;
    for (uint32_t i = 0; i < hdr->frames; i++) {
        if (glif_reader_decode(&gr, i) != 0) {
            fprintf(stderr, "Decode error at frame %u (type=%d)\n",
                    i, gr.index[i].type);
            errors++;
            continue;
        }
        /* Sanity check: chars should be 32-126 */
        const uint8_t *data = glif_reader_frame_data(&gr);
        for (int c = 0; c < cells; c++) {
            uint8_t ch = data[c * 4];
            if (ch < 32 || ch > 126) {
                if (errors < 20)
                    fprintf(stderr, "Frame %u cell %d: bad char %d\n", i, c, ch);
                errors++;
            }
        }
    }
    printf("Decoded %u frames, %d errors\n", hdr->frames, errors);

    /* Dump a specific frame if requested */
    int dump = 0;
    if (argc > 2) {
        char *endptr;
        long val = strtol(argv[2], &endptr, 10);
        if (*endptr != '\0' || val < 0 || val >= (long)hdr->frames) {
            dump = val < 0 ? 0 : (val >= (long)hdr->frames ? (int)hdr->frames - 1 : (int)val);
        } else {
            dump = (int)val;
        }
    }
    if (glif_reader_decode(&gr, (uint32_t)dump) == 0) {
        const uint8_t *data = glif_reader_frame_data(&gr);
        printf("\nFrame %d (type=%d), first 40 cells:\n", dump, gr.index[dump].type);
        for (int i = 0; i < 40 && i < cells; i++) {
            printf("  [%d] ch='%c'(%d) rgb=(%d,%d,%d)\n", i,
                   data[i*4] >= 32 && data[i*4] <= 126 ? data[i*4] : '?',
                   data[i*4], data[i*4+1], data[i*4+2], data[i*4+3]);
        }

        /* Print ASCII grid of first few rows */
        printf("\nASCII preview (first 8 rows):\n");
        int preview_rows = hdr->rows < 8 ? hdr->rows : 8;
        for (int r = 0; r < preview_rows; r++) {
            for (int c = 0; c < (int)hdr->cols; c++) {
                uint8_t ch = data[(r * hdr->cols + c) * 4];
                putchar(ch >= 32 && ch <= 126 ? ch : '?');
            }
            putchar('\n');
        }
    }

    glif_reader_close(&gr);
    free(buf);
    return errors > 0 ? 1 : 0;
}
