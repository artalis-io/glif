#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

typedef struct {
    int width;
    int height;
    int channels;
    uint8_t *pixels;
} Image;

typedef struct {
    int width;
    int height;
    float *data;
} LightnessMap;

/* Load image from file. Returns 0 on success, -1 on failure. */
int image_load(Image *img, const char *path);
void image_free(Image *img);

/* Compute relative luminance lightness map (0–1 per pixel). */
int lightness_map_create(LightnessMap *lm, const Image *img);

/* Recompute lightness values in-place (reuses lm->data buffer). */
int lightness_map_update(LightnessMap *lm, const Image *img);

void lightness_map_free(LightnessMap *lm);

#endif
