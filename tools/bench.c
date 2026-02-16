#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "image.h"
#include "sampling.h"
#include "grid.h"
#include "font.h"
#include "contrast.h"
#include "match.h"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: bench <image> <font>\n");
        return 1;
    }

    int cell_w = 10, cell_h = 20;
    float dir_crunch = 2.0f, global_crunch = 2.0f;
    double t0, t1;

    printf("=== One-time costs ===\n");

    t0 = now_ms();
    Image img;
    image_load(&img, argv[1]);
    t1 = now_ms();
    printf("image_load:          %6.2f ms\n", t1 - t0);

    SamplingConfig sc;
    sampling_config_init(&sc);

    t0 = now_ms();
    CharDatabase db;
    char_db_create(&db, argv[2], cell_w, cell_h, &sc);
    t1 = now_ms();
    printf("char_db_create:      %6.2f ms\n", t1 - t0);

    /* Precompute once */
    LightnessMap lm;
    lightness_map_create(&lm, &img);

    t0 = now_ms();
    PrecomputedMasks pm;
    sampling_precompute(&pm, &sc, cell_w, cell_h, lm.width);
    t1 = now_ms();
    printf("precompute_masks:    %6.2f ms\n", t1 - t0);

    lightness_map_free(&lm);

    printf("\n=== Per-frame pipeline (single run) ===\n");

    /* Warm up */
    lightness_map_create(&lm, &img);
    lightness_map_free(&lm);

    lightness_map_create(&lm, &img);

    t0 = now_ms();
    LightnessMap lm2;
    lightness_map_create(&lm2, &img);
    t1 = now_ms();
    printf("lightness_map:       %6.2f ms\n", t1 - t0);
    lightness_map_free(&lm2);

    t0 = now_ms();
    Grid grid;
    grid_create(&grid, &img, cell_w, cell_h);
    t1 = now_ms();
    printf("grid_create:         %6.2f ms\n", t1 - t0);

    t0 = now_ms();
    grid_compute_vectors_fast(&grid, &lm, &pm);
    t1 = now_ms();
    printf("grid_vectors_fast:   %6.2f ms\n", t1 - t0);

    /* Also benchmark old path for comparison */
    Grid grid_old;
    grid_create(&grid_old, &img, cell_w, cell_h);
    t0 = now_ms();
    grid_compute_vectors(&grid_old, &lm, &sc);
    t1 = now_ms();
    printf("grid_vectors_old:    %6.2f ms\n", t1 - t0);
    grid_free(&grid_old);

    t0 = now_ms();
    grid_compute_colors(&grid, &img);
    t1 = now_ms();
    printf("grid_colors:         %6.2f ms\n", t1 - t0);

    t0 = now_ms();
    contrast_directional(&grid, &sc, dir_crunch);
    t1 = now_ms();
    printf("contrast_dir:        %6.2f ms\n", t1 - t0);

    t0 = now_ms();
    contrast_global(&grid, global_crunch);
    t1 = now_ms();
    printf("contrast_global:     %6.2f ms\n", t1 - t0);

    t0 = now_ms();
    match_grid(&grid, &db);
    t1 = now_ms();
    printf("match_grid:          %6.2f ms\n", t1 - t0);

    grid_free(&grid);
    lightness_map_free(&lm);

    /* Steady-state benchmark */
    int N = 100;
    printf("\n=== %d-iteration average ===\n", N);
    double total = 0;
    for (int run = 0; run < N; run++) {
        t0 = now_ms();

        LightnessMap lm3;
        lightness_map_create(&lm3, &img);

        Grid g;
        grid_create(&g, &img, cell_w, cell_h);
        grid_compute_vectors_fast(&g, &lm3, &pm);
        grid_compute_colors(&g, &img);
        contrast_directional(&g, &sc, dir_crunch);
        contrast_global(&g, global_crunch);
        match_grid(&g, &db);

        t1 = now_ms();
        total += (t1 - t0);

        grid_free(&g);
        lightness_map_free(&lm3);
    }

    double avg = total / N;
    printf("avg per-frame:       %6.2f ms  (%.0f fps)\n", avg, 1000.0 / avg);
    printf("\nGrid: %d x %d = %d cells\n",
           img.width / cell_w, img.height / cell_h,
           (img.width / cell_w) * (img.height / cell_h));
    printf("Image: %d x %d = %d pixels\n",
           img.width, img.height, img.width * img.height);

    sampling_precompute_free(&pm);
    char_db_free(&db);
    image_free(&img);
    return 0;
}
