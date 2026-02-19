#ifndef GLIF_IMAGE_H
#define GLIF_IMAGE_H

#include <stdint.h>

typedef struct {
    int width;
    int height;
    int channels;
    uint8_t *pixels;
    int owns_pixels;  /* 1 = stbi-allocated, 0 = external buffer */
} GlifImage;

typedef struct {
    int width;
    int height;
    float *data;
} GlifLightnessMap;

/* Load image from file. Returns 0 on success, -1 on failure. */
int glif_image_load(GlifImage *img, const char *path);

/* Wrap an existing pixel buffer (no copy). channels must be 3 (RGB) or 4 (RGBA). */
int glif_image_load_buffer(GlifImage *img, const uint8_t *data, int w, int h, int channels);

void glif_image_free(GlifImage *img);

/* Compute relative luminance lightness map (0–1 per pixel). */
int glif_lightness_map_create(GlifLightnessMap *lm, const GlifImage *img);

/* Recompute lightness values in-place (reuses lm->data buffer). */
int glif_lightness_map_update(GlifLightnessMap *lm, const GlifImage *img);

/* Per-frame normalization: remap lightness values so the frame's
 * dynamic range fills 0–1. Uses p5/p95 percentiles as bounds.
 * Pixels below p5 stay at 0 (background noise → space). */
void glif_lightness_map_normalize(GlifLightnessMap *lm);

void glif_lightness_map_free(GlifLightnessMap *lm);

#endif
