/* Re-encode a .glif file with v3 compression. */
#include "glif.h"
#include "output.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.glif> <output.glif> [keyframe_interval]\n", argv[0]);
        return 1;
    }
#if defined(__OpenBSD__) || defined(__linux__)
    #include "sandbox.h"
    glif_unveil(argv[1], "r");
    glif_unveil(argv[2], "wc");
    glif_unveil_lock();
    if (glif_pledge("stdio rpath wpath cpath") != 0)
        fprintf(stderr, "warning: pledge() failed\n");
#endif

    int kf_interval = 0;
    if (argc >= 4) {
        char *end;
        long val = strtol(argv[3], &end, 10);
        if (*end != '\0' || val < 0 || val > INT_MAX) {
            fprintf(stderr, "error: invalid keyframe_interval\n");
            return 1;
        }
        kf_interval = (int)val;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "error: cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    if (flen < 0) { fprintf(stderr, "error: ftell failed\n"); fclose(f); return 1; }
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
    fprintf(stderr, "Input: v%d, %dx%d, %d frames, %.1f fps\n",
            hdr->version, hdr->cols, hdr->rows, hdr->frames, hdr->fps);

    /* Create writer with compression enabled */
    GlifWriter gw;
    int dark = (hdr->flags & GLIF_FLAG_DARK) ? 1 : 0;
    if (glif_writer_init_v2(&gw, argv[2], hdr->cols, hdr->rows,
                             hdr->cell_w, hdr->cell_h, hdr->fps, dark, 1) != 0) {
        fprintf(stderr, "error: cannot create output\n");
        glif_reader_close(&gr); free(data); return 1;
    }
    if (kf_interval > 0)
        glif_writer_set_keyframe_interval(&gw, kf_interval);

    /* Transcode each frame */
    GlifGrid grid;
    grid.rows = hdr->rows;
    grid.cols = hdr->cols;
    grid.cell_w = hdr->cell_w;
    grid.cell_h = hdr->cell_h;
    int total = hdr->cols * hdr->rows;
    grid.cells = malloc((size_t)total * sizeof(GlifGridCell));
    if (!grid.cells) {
        fprintf(stderr, "error: alloc failed\n");
        glif_writer_finish(&gw); glif_reader_close(&gr); free(data); return 1;
    }

    for (uint32_t i = 0; i < hdr->frames; i++) {
        if (glif_reader_decode(&gr, i) != 0) {
            fprintf(stderr, "error: decode frame %u failed\n", i);
            break;
        }
        const uint8_t *frame = glif_reader_frame_data(&gr);
        for (int j = 0; j < total; j++) {
            grid.cells[j].ch = (char)frame[j * 4];
            grid.cells[j].r = frame[j * 4 + 1];
            grid.cells[j].g = frame[j * 4 + 2];
            grid.cells[j].b = frame[j * 4 + 3];
            grid.cells[j].px = (j % grid.cols) * grid.cell_w;
            grid.cells[j].py = (j / grid.cols) * grid.cell_h;
        }
        glif_writer_frame(&gw, &grid);

        if ((i + 1) % 500 == 0)
            fprintf(stderr, "  %u / %u frames\n", i + 1, hdr->frames);
    }

    if (glif_writer_finish(&gw) != 0) {
        fprintf(stderr, "error: write failed\n");
    }

    free(grid.cells);
    glif_reader_close(&gr);
    free(data);

    /* Report sizes */
    FILE *out = fopen(argv[2], "rb");
    if (out) {
        fseek(out, 0, SEEK_END);
        long out_len = ftell(out);
        fclose(out);
        fprintf(stderr, "Input:  %ld bytes (%.1f MB)\n", flen, flen / 1048576.0);
        fprintf(stderr, "Output: %ld bytes (%.1f MB)\n", out_len, out_len / 1048576.0);
        fprintf(stderr, "Ratio:  %.2fx\n", (double)flen / out_len);
    }

    return 0;
}
