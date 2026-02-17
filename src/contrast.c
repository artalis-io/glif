#include "contrast.h"
#include <math.h>
#include <stddef.h>

void contrast_directional(Grid *grid, const SamplingConfig *sc,
                          float crunch) {
    int ncells = grid->rows * grid->cols;

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < ncells; i++) {
        GridCell *cell = &grid->cells[i];

        /* For each internal circle, find max of (internal, affecting externals),
         * then apply: value = (value / maxValue)^crunch * maxValue */
        for (int s = 0; s < NUM_INTERNAL; s++) {
            float val = cell->shape.v[s];
            float max_val = val;

            for (int a = 0; a < sc->affecting_count[s]; a++) {
                int ext_idx = sc->affecting[s][a];
                if (cell->external.v[ext_idx] > max_val)
                    max_val = cell->external.v[ext_idx];
            }

            if (max_val > 1e-8f) {
                cell->shape.v[s] = powf(val / max_val, crunch) * max_val;
            }
        }
    }
}

void contrast_global(Grid *grid, float crunch) {
    int ncells = grid->rows * grid->cols;

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < ncells; i++) {
        GridCell *cell = &grid->cells[i];

        float max_c = vec6_max_component(cell->shape);
        if (max_c > 1e-8f) {
            for (int s = 0; s < NUM_INTERNAL; s++) {
                float val = cell->shape.v[s];
                cell->shape.v[s] = powf(val / max_c, crunch) * max_c;
            }
        }
    }
}
