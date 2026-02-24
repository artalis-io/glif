#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "image.h"
#include "sampling.h"
#include "grid.h"
#include "font.h"
#include "contrast.h"
#include "match.h"
#include "compress.h"
#include "output.h"
#include "glif.h"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* Flatten grid to [ch, r, g, b] buffer */
static void flatten_grid(const GlifGrid *grid, uint8_t *out) {
    int total = grid->rows * grid->cols;
    for (int i = 0; i < total; i++) {
        const GlifGridCell *cell = &grid->cells[i];
        out[i * 4]     = (uint8_t)cell->ch;
        out[i * 4 + 1] = cell->r;
        out[i * 4 + 2] = cell->g;
        out[i * 4 + 3] = cell->b;
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: bench <image> <font>\n");
        return 1;
    }

#if defined(__OpenBSD__) || defined(__linux__)
    #include "sandbox.h"
    glif_unveil(argv[1], "r");
    glif_unveil(argv[2], "r");
    glif_unveil("/tmp", "rwc");
    glif_unveil_lock();
    if (glif_pledge("stdio rpath wpath cpath") != 0)
        fprintf(stderr, "warning: pledge() failed\n");
#endif

    int cell_w = 10, cell_h = 20;
    float dir_crunch = 2.0f, global_crunch = 2.0f;
    double t0, t1;

    printf("=== One-time costs ===\n");

    t0 = now_ms();
    GlifImage img;
    if (glif_image_load(&img, argv[1]) != 0) {
        fprintf(stderr, "error: failed to load image '%s'\n", argv[1]);
        return 1;
    }
    t1 = now_ms();
    printf("glif_image_load:          %6.2f ms\n", t1 - t0);

    GlifSamplingConfig sc;
    glif_sampling_config_init(&sc);

    t0 = now_ms();
    GlifCharDatabase db;
    if (glif_char_db_create(&db, argv[2], cell_w, cell_h, &sc) != 0) {
        fprintf(stderr, "error: failed to create char db from '%s'\n", argv[2]);
        glif_image_free(&img);
        return 1;
    }
    t1 = now_ms();
    printf("glif_char_db_create:      %6.2f ms\n", t1 - t0);

    /* Precompute once */
    GlifLightnessMap lm;
    if (glif_lightness_map_create(&lm, &img) != 0) {
        fprintf(stderr, "error: failed to create lightness map\n");
        glif_char_db_free(&db);
        glif_image_free(&img);
        return 1;
    }

    t0 = now_ms();
    GlifPrecomputedMasks pm;
    glif_sampling_precompute(&pm, &sc, cell_w, cell_h, lm.width);
    t1 = now_ms();
    printf("precompute_masks:    %6.2f ms\n", t1 - t0);

    glif_lightness_map_free(&lm);

    printf("\n=== Per-frame pipeline (single run) ===\n");

    /* Warm up */
    glif_lightness_map_create(&lm, &img);
    glif_lightness_map_free(&lm);

    glif_lightness_map_create(&lm, &img);

    t0 = now_ms();
    GlifLightnessMap lm2;
    glif_lightness_map_create(&lm2, &img);
    t1 = now_ms();
    printf("lightness_map:       %6.2f ms\n", t1 - t0);
    glif_lightness_map_free(&lm2);

    t0 = now_ms();
    GlifGrid grid;
    glif_grid_create(&grid, &img, cell_w, cell_h);
    t1 = now_ms();
    printf("glif_grid_create:         %6.2f ms\n", t1 - t0);

    t0 = now_ms();
    glif_grid_compute_vectors_fast(&grid, &lm, &pm);
    t1 = now_ms();
    printf("grid_vectors_fast:   %6.2f ms\n", t1 - t0);

    /* Also benchmark old path for comparison */
    GlifGrid grid_old;
    glif_grid_create(&grid_old, &img, cell_w, cell_h);
    t0 = now_ms();
    glif_grid_compute_vectors(&grid_old, &lm, &sc);
    t1 = now_ms();
    printf("grid_vectors_old:    %6.2f ms\n", t1 - t0);
    glif_grid_free(&grid_old);

    t0 = now_ms();
    glif_grid_compute_colors(&grid, &img);
    t1 = now_ms();
    printf("grid_colors:         %6.2f ms\n", t1 - t0);

    t0 = now_ms();
    glif_contrast_directional(&grid, &sc, dir_crunch, NULL);
    t1 = now_ms();
    printf("contrast_dir:        %6.2f ms\n", t1 - t0);

    t0 = now_ms();
    glif_contrast_global(&grid, global_crunch, NULL);
    t1 = now_ms();
    printf("glif_contrast_global:     %6.2f ms\n", t1 - t0);

    t0 = now_ms();
    glif_match_grid(&grid, &db);
    t1 = now_ms();
    printf("glif_match_grid:          %6.2f ms\n", t1 - t0);

    /* ── Encode/decode benchmarks ── */
    int cells = grid.rows * grid.cols;
    size_t buf_size = (size_t)cells * 4;
    uint8_t *flat = malloc(buf_size);
    flatten_grid(&grid, flat);

    size_t enc_cap = glif_compress_deflate_bound(cells);
    uint8_t *enc_buf = malloc(enc_cap);
    uint8_t *dec_buf = malloc(buf_size);
    uint8_t *work_buf = malloc(buf_size);
    uint8_t *zeroed = calloc(buf_size, 1);
    if (!flat || !enc_buf || !dec_buf || !work_buf || !zeroed) {
        fprintf(stderr, "error: allocation failed\n");
        free(flat); free(enc_buf); free(dec_buf); free(work_buf); free(zeroed);
        glif_grid_free(&grid);
        glif_lightness_map_free(&lm);
        glif_sampling_precompute_free(&pm);
        glif_char_db_free(&db);
        glif_image_free(&img);
        return 1;
    }

    int enc_iters = 1000;
    printf("\n=== Encode/decode (%d iterations) ===\n", enc_iters);

    /* Deflate */
    int deflate_size = glif_compress_deflate_encode(flat, cells, enc_buf, enc_cap);
    t0 = now_ms();
    for (int i = 0; i < enc_iters; i++) {
        glif_compress_deflate_encode(flat, cells, enc_buf, enc_cap);
        glif_compress_deflate_decode(enc_buf, (size_t)deflate_size, dec_buf, cells);
    }
    t1 = now_ms();
    printf("Deflate:        %5d bytes  (%.1f%%)  %6.2f ms\n",
           deflate_size, 100.0 * deflate_size / (double)buf_size, (t1 - t0) / enc_iters);

    /* Delta+Deflate (vs zeroed prev) */
    int dd_size = glif_compress_delta_deflate_encode(flat, zeroed, cells,
                                                      work_buf, enc_buf, enc_cap);
    t0 = now_ms();
    for (int i = 0; i < enc_iters; i++) {
        glif_compress_delta_deflate_encode(flat, zeroed, cells, work_buf, enc_buf, enc_cap);
        glif_compress_delta_deflate_decode(enc_buf, (size_t)dd_size, zeroed,
                                            work_buf, dec_buf, cells);
    }
    t1 = now_ms();
    printf("Delta+Deflate:  %5d bytes  (%.1f%%)  %6.2f ms\n",
           dd_size, 100.0 * dd_size / (double)buf_size, (t1 - t0) / enc_iters);

    printf("Raw:            %5d bytes\n", (int)buf_size);

    glif_grid_free(&grid);
    glif_lightness_map_free(&lm);

    /* ── Combined pipeline + encode FPS ── */
    int N = 100;
    printf("\n=== %d-iteration average ===\n", N);
    double total = 0;
    double total_streaming = 0;
    for (int run = 0; run < N; run++) {
        t0 = now_ms();

        GlifLightnessMap lm3;
        glif_lightness_map_create(&lm3, &img);

        GlifGrid g;
        glif_grid_create(&g, &img, cell_w, cell_h);
        glif_grid_compute_vectors_fast(&g, &lm3, &pm);
        glif_grid_compute_colors(&g, &img);
        glif_contrast_directional(&g, &sc, dir_crunch, NULL);
        glif_contrast_global(&g, global_crunch, NULL);
        glif_match_grid(&g, &db);

        t1 = now_ms();
        total += (t1 - t0);

        /* GLIF streaming: flatten + try deflate encodings + pick best */
        double ts0 = now_ms();
        int c = g.rows * g.cols;
        flatten_grid(&g, flat);

        int best = (int)buf_size;
        int ds = glif_compress_deflate_encode(flat, c, enc_buf, enc_cap);
        if (ds > 0 && ds < best) best = ds;

        if (run > 0) {
            int dds = glif_compress_delta_deflate_encode(flat, zeroed, c,
                                                          work_buf, enc_buf, enc_cap);
            if (dds > 0 && dds < best) best = dds;
        }
        memcpy(zeroed, flat, buf_size);

        double ts1 = now_ms();
        total_streaming += (t1 - t0) + (ts1 - ts0);

        glif_grid_free(&g);
        glif_lightness_map_free(&lm3);
    }

    double avg = total / N;
    double avg_stream = total_streaming / N;
    printf("avg per-frame:       %6.2f ms  (%.0f fps)\n", avg, 1000.0 / avg);
    printf("GLIF streaming:      %6.2f ms  (%.0f fps)\n", avg_stream, 1000.0 / avg_stream);
    printf("\nGrid: %d x %d = %d cells\n",
           img.width / cell_w, img.height / cell_h,
           (img.width / cell_w) * (img.height / cell_h));
    printf("GlifImage: %d x %d = %d pixels\n",
           img.width, img.height, img.width * img.height);

    /* ── Decode benchmark ── */
    printf("\n=== Decode benchmark ===\n");
    {
        /* Write a compressed .glif with N frames */
        const char *tmp_path = "/tmp/glif_bench_decode.glif";
        int gcols = img.width / cell_w;
        int grows = img.height / cell_h;

        GlifWriter gw;
        if (glif_writer_init_v2(&gw, tmp_path, gcols, grows, cell_w, cell_h,
                                 30.0f, 0, 1) == 0) {
            for (int run = 0; run < N; run++) {
                GlifLightnessMap lm4;
                glif_lightness_map_create(&lm4, &img);
                GlifGrid g;
                glif_grid_create(&g, &img, cell_w, cell_h);
                glif_grid_compute_vectors_fast(&g, &lm4, &pm);
                glif_grid_compute_colors(&g, &img);
                glif_contrast_directional(&g, &sc, dir_crunch, NULL);
                glif_contrast_global(&g, global_crunch, NULL);
                glif_match_grid(&g, &db);
                glif_writer_frame(&gw, &g);
                glif_grid_free(&g);
                glif_lightness_map_free(&lm4);
            }
            glif_writer_finish(&gw);

            /* Read buffer */
            FILE *f = fopen(tmp_path, "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long flen = ftell(f);
                if (flen <= 0) { fclose(f); goto decode_done; }
                rewind(f);
                uint8_t *fbuf = malloc((size_t)flen);
                if (fbuf && fread(fbuf, 1, (size_t)flen, f) == (size_t)flen) {
                    fclose(f);

                    GlifReader gr;
                    if (glif_reader_open(&gr, fbuf, (size_t)flen) == 0) {
                        t0 = now_ms();
                        for (uint32_t i = 0; i < gr.header.frames; i++) {
                            glif_reader_decode(&gr, i);
                        }
                        t1 = now_ms();
                        double dec_avg = (t1 - t0) / gr.header.frames;
                        printf("decode:          %6.3f ms/frame  (%.0f fps)  [%u frames]\n",
                               dec_avg, 1000.0 / dec_avg, gr.header.frames);
                        glif_reader_close(&gr);
                    }
                    free(fbuf);
                } else {
                    if (fbuf) free(fbuf);
                    fclose(f);
                }
            }
            remove(tmp_path);
        }
    decode_done: ;
    }

    free(flat);
    free(enc_buf);
    free(dec_buf);
    free(work_buf);
    free(zeroed);
    glif_sampling_precompute_free(&pm);
    glif_char_db_free(&db);
    glif_image_free(&img);
    return 0;
}
