/* Compare v1 (raw) vs v2 (compressed) .glif decode byte-for-byte.
   Encodes same pipeline output to both, decodes both, compares every cell. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image.h"
#include "sampling.h"
#include "grid.h"
#include "font.h"
#include "contrast.h"
#include "match.h"
#include "output.h"
#include "glif.h"

static uint8_t *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) { fclose(f); *out_len = 0; return NULL; }
    rewind(f);
    uint8_t *buf = malloc((size_t)len);
    if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        *out_len = 0;
        return NULL;
    }
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: glif_compare <image> <font> [num_frames]\n");
        return 1;
    }

    int num_frames = 20;
    if (argc > 3) {
        char *endptr;
        long val = strtol(argv[3], &endptr, 10);
        if (*endptr == '\0' && val >= 1 && val <= 10000) {
            num_frames = (int)val;
        }
    }
    int cell_w = 4, cell_h = 8;
    float dir_crunch = 2.5f, global_crunch = 2.5f;

    GlifImage img;
    if (glif_image_load(&img, argv[1]) != 0) {
        fprintf(stderr, "Failed to load image\n");
        return 1;
    }

    GlifSamplingConfig sc;
    glif_sampling_config_init(&sc);

    GlifCharDatabase db;
    if (glif_char_db_create(&db, argv[2], cell_w, cell_h, &sc) != 0) {
        fprintf(stderr, "Failed to create char database\n");
        glif_image_free(&img);
        return 1;
    }

    GlifLightnessMap lm;
    if (glif_lightness_map_create(&lm, &img) != 0) {
        fprintf(stderr, "Failed to create lightness map\n");
        glif_char_db_free(&db);
        glif_image_free(&img);
        return 1;
    }

    GlifPrecomputedMasks pm;
    if (glif_sampling_precompute(&pm, &sc, cell_w, cell_h, lm.width) != 0) {
        fprintf(stderr, "Failed to precompute sampling masks\n");
        glif_lightness_map_free(&lm);
        glif_char_db_free(&db);
        glif_image_free(&img);
        return 1;
    }

    int cols = img.width / cell_w;
    int rows = img.height / cell_h;
    int cells = cols * rows;

    printf("Grid: %dx%d (%d cells), encoding %d frames\n", cols, rows, cells, num_frames);

    /* Write v1 (raw) and v2 (compressed) */
    const char *v1_path = "/tmp/glif_cmp_v1.glif";
    const char *v2_path = "/tmp/glif_cmp_v2.glif";

    GlifWriter gw1, gw2;
    glif_writer_init_v2(&gw1, v1_path, cols, rows, cell_w, cell_h, 30.0f, 1, 0);
    glif_writer_init_v2(&gw2, v2_path, cols, rows, cell_w, cell_h, 30.0f, 1, 1);

    /* Also keep raw frame data for direct comparison */
    uint8_t **raw_frames = malloc((size_t)num_frames * sizeof(uint8_t *));
    if (!raw_frames) {
        fprintf(stderr, "Failed to allocate raw_frames\n");
        glif_writer_finish(&gw1);
        glif_writer_finish(&gw2);
        glif_sampling_precompute_free(&pm);
        glif_lightness_map_free(&lm);
        glif_char_db_free(&db);
        glif_image_free(&img);
        return 1;
    }

    for (int i = 0; i < num_frames; i++) {
        GlifGrid g;
        glif_grid_create(&g, &img, cell_w, cell_h);
        glif_grid_compute_vectors_fast(&g, &lm, &pm);
        glif_grid_compute_colors(&g, &img);
        glif_contrast_directional(&g, &sc, dir_crunch, NULL);
        glif_contrast_global(&g, global_crunch, NULL);
        glif_match_grid(&g, &db);

        /* Slightly vary grid to simulate different frames */
        for (int c = 0; c < cells && c < i * 50; c++) {
            g.cells[c].r = (uint8_t)((g.cells[c].r + i * 7) & 0xFF);
            g.cells[c].g = (uint8_t)((g.cells[c].g + i * 3) & 0xFF);
        }

        glif_writer_frame(&gw1, &g);
        glif_writer_frame(&gw2, &g);

        /* Save raw [ch, r, g, b] for comparison */
        raw_frames[i] = malloc((size_t)cells * 4);
        for (int c = 0; c < cells; c++) {
            raw_frames[i][c * 4]     = (uint8_t)g.cells[c].ch;
            raw_frames[i][c * 4 + 1] = g.cells[c].r;
            raw_frames[i][c * 4 + 2] = g.cells[c].g;
            raw_frames[i][c * 4 + 3] = g.cells[c].b;
        }

        glif_grid_free(&g);
    }

    glif_writer_finish(&gw1);
    glif_writer_finish(&gw2);

    /* Read both files */
    size_t v1_len, v2_len;
    uint8_t *v1_buf = read_file(v1_path, &v1_len);
    uint8_t *v2_buf = read_file(v2_path, &v2_len);
    printf("File sizes: v1=%zu bytes, v2=%zu bytes (%.1f%% of v1)\n",
           v1_len, v2_len, 100.0 * v2_len / v1_len);

    /* Open readers */
    GlifReader gr1, gr2;
    if (glif_reader_open(&gr1, v1_buf, v1_len) != 0) {
        fprintf(stderr, "Failed to open v1\n"); return 1;
    }
    if (glif_reader_open(&gr2, v2_buf, v2_len) != 0) {
        fprintf(stderr, "Failed to open v2\n"); return 1;
    }

    /* Compare every frame: raw source vs v1 decode vs v2 decode */
    int mismatches = 0;
    for (int i = 0; i < num_frames; i++) {
        glif_reader_decode(&gr1, (uint32_t)i);
        glif_reader_decode(&gr2, (uint32_t)i);

        const uint8_t *d1 = glif_reader_frame_data(&gr1);
        const uint8_t *d2 = glif_reader_frame_data(&gr2);
        const uint8_t *raw = raw_frames[i];

        /* Compare raw vs v1 decode */
        if (memcmp(raw, d1, (size_t)cells * 4) != 0) {
            printf("MISMATCH: frame %d raw vs v1 decode\n", i);
            for (int c = 0; c < cells * 4; c++) {
                if (raw[c] != d1[c]) {
                    printf("  byte %d (cell %d, %s): raw=%d, v1=%d\n",
                           c, c/4, (c%4==0)?"ch":(c%4==1)?"r":(c%4==2)?"g":"b",
                           raw[c], d1[c]);
                    if (++mismatches > 10) break;
                }
            }
        }

        /* Compare raw vs v2 decode */
        if (memcmp(raw, d2, (size_t)cells * 4) != 0) {
            printf("MISMATCH: frame %d raw vs v2 decode\n", i);
            for (int c = 0; c < cells * 4; c++) {
                if (raw[c] != d2[c]) {
                    printf("  byte %d (cell %d, %s): raw=%d, v2=%d\n",
                           c, c/4, (c%4==0)?"ch":(c%4==1)?"r":(c%4==2)?"g":"b",
                           raw[c], d2[c]);
                    if (++mismatches > 10) break;
                }
            }
        }

        /* Compare v1 vs v2 */
        if (memcmp(d1, d2, (size_t)cells * 4) != 0) {
            printf("MISMATCH: frame %d v1 vs v2\n", i);
            mismatches++;
        }
    }

    if (mismatches == 0) {
        printf("\nAll %d frames match byte-exact: raw == v1 == v2\n", num_frames);
        printf("Characters AND colors are identical across all encodings.\n");
    } else {
        printf("\n%d mismatches found!\n", mismatches);
    }

    /* Cleanup */
    glif_reader_close(&gr1);
    glif_reader_close(&gr2);
    for (int i = 0; i < num_frames; i++) free(raw_frames[i]);
    free(raw_frames);
    free(v1_buf);
    free(v2_buf);
    glif_sampling_precompute_free(&pm);
    glif_lightness_map_free(&lm);
    glif_char_db_free(&db);
    glif_image_free(&img);
    remove(v1_path);
    remove(v2_path);
    return mismatches > 0 ? 1 : 0;
}
