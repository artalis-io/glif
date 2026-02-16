#ifndef CONTRAST_H
#define CONTRAST_H

#include "grid.h"
#include "sampling.h"

/* Apply directional contrast enhancement using external sampling circles.
 * For each internal circle, compute how different it is from its neighboring
 * external circles, then exponentiate to crunch contrast. */
void contrast_directional(Grid *grid, const SamplingConfig *sc,
                          float crunch);

/* Apply global contrast enhancement: normalize each cell's shape vector
 * by its max component, then exponentiate. */
void contrast_global(Grid *grid, float crunch);

#endif
