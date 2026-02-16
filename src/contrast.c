#include "contrast.h"
#include <math.h>
#include <stddef.h>

void contrast_directional(Grid *grid, const SamplingConfig *sc,
                          float crunch) {
    int ncells = grid->rows * grid->cols;

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < ncells; i++) {
        GridCell *cell = &grid->cells[i];

        /* For each internal circle, compute max absolute difference
         * with its affecting external circles */
        Vec6 enhanced;
        for (int s = 0; s < NUM_INTERNAL; s++) {
            float internal_val = cell->shape.v[s];
            float max_diff = 0.0f;

            for (int a = 0; a < sc->affecting_count[s]; a++) {
                int ext_idx = sc->affecting[s][a];
                float diff = fabsf(internal_val - cell->external.v[ext_idx]);
                if (diff > max_diff) max_diff = diff;
            }
            enhanced.v[s] = max_diff;
        }

        /* Normalize the directional contrast vector */
        float len = vec6_length(enhanced);
        if (len > 1e-8f) {
            enhanced = vec6_scale(enhanced, 1.0f / len);
        }

        /* Exponentiate (crunch) */
        for (int s = 0; s < NUM_INTERNAL; s++) {
            enhanced.v[s] = powf(enhanced.v[s], crunch);
        }

        cell->shape = enhanced;
    }
}

void contrast_global(Grid *grid, float crunch) {
    int ncells = grid->rows * grid->cols;

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < ncells; i++) {
        GridCell *cell = &grid->cells[i];

        float max_c = vec6_max_component(cell->shape);
        if (max_c > 1e-8f) {
            cell->shape = vec6_scale(cell->shape, 1.0f / max_c);
        }

        for (int s = 0; s < NUM_INTERNAL; s++) {
            cell->shape.v[s] = powf(cell->shape.v[s], crunch);
        }

        /* Re-normalize after global contrast */
        cell->shape = vec6_normalize(cell->shape);
    }
}
