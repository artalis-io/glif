#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

typedef struct {
    int width;
    int height;
    int channels;
    uint8_t *pixels;
    int owns_pixels;  /* 1 = stbi-allocated, 0 = external buffer */
} Image;

typedef struct {
    int width;
    int height;
    float *data;
} LightnessMap;

/* Load image from file. Returns 0 on success, -1 on failure. */
int image_load(Image *img, const char *path);

/* Wrap an existing pixel buffer (no copy). channels must be 3 (RGB) or 4 (RGBA). */
int image_load_buffer(Image *img, const uint8_t *data, int w, int h, int channels);

void image_free(Image *img);

/* Compute relative luminance lightness map (0–1 per pixel). */
int lightness_map_create(LightnessMap *lm, const Image *img);

/* Recompute lightness values in-place (reuses lm->data buffer). */
int lightness_map_update(LightnessMap *lm, const Image *img);

/* Per-frame normalization: remap lightness values so the frame's
 * dynamic range fills 0–1. Uses p5/p95 percentiles as bounds.
 * Pixels below p5 stay at 0 (background noise → space). */
void lightness_map_normalize(LightnessMap *lm);

void lightness_map_free(LightnessMap *lm);

#endif
