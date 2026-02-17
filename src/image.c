#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "image.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

int image_load(Image *img, const char *path) {
    /* Force 3 channels (RGB) to avoid OOB on grayscale images (H6) */
    int orig_channels;
    img->pixels = stbi_load(path, &img->width, &img->height, &orig_channels, 3);
    if (!img->pixels) {
        fprintf(stderr, "error: failed to load image '%s': %s\n",
                path, stbi_failure_reason());
        return -1;
    }
    img->channels = 3;
    return 0;
}

void image_free(Image *img) {
    if (!img) return;
    if (img->pixels) {
        stbi_image_free(img->pixels);
        img->pixels = NULL;
    }
}

/* sRGB -> linear LUT: precomputed for all 256 byte values */
static float srgb_lut[256];
static int srgb_lut_ready = 0;

static void srgb_lut_init(void) {
    if (srgb_lut_ready) return;
    for (int i = 0; i < 256; i++) {
        float c = (float)i / 255.0f;
        if (c <= 0.04045f)
            srgb_lut[i] = c / 12.92f;
        else
            srgb_lut[i] = powf((c + 0.055f) / 1.055f, 2.4f);
    }
    srgb_lut_ready = 1;
}

/* Recompute lightness values in-place (caller must provide pre-allocated lm->data).
 * Used in video mode to avoid per-frame malloc/free. */
int lightness_map_update(LightnessMap *lm, const Image *img) {
    srgb_lut_init();

    size_t npixels = (size_t)img->width * (size_t)img->height;
    const uint8_t *pixels = img->pixels;
    int channels = img->channels;
    float *dest = lm->data;

    #pragma omp parallel for schedule(static)
    for (size_t i = 0; i < npixels; i++) {
        const uint8_t *px = pixels + i * channels;
        dest[i] = 0.2126f * srgb_lut[px[0]]
                + 0.7152f * srgb_lut[px[1]]
                + 0.0722f * srgb_lut[px[2]];
    }
    return 0;
}

int lightness_map_create(LightnessMap *lm, const Image *img) {
    srgb_lut_init();

    lm->width = img->width;
    lm->height = img->height;
    lm->data = malloc((size_t)lm->width * (size_t)lm->height * sizeof(float));
    if (!lm->data) return -1;

    size_t npixels = (size_t)img->width * (size_t)img->height;
    const uint8_t *pixels = img->pixels;
    int channels = img->channels;
    float *dest = lm->data;

    #pragma omp parallel for schedule(static)
    for (size_t i = 0; i < npixels; i++) {
        const uint8_t *px = pixels + i * channels;
        dest[i] = 0.2126f * srgb_lut[px[0]]
                + 0.7152f * srgb_lut[px[1]]
                + 0.0722f * srgb_lut[px[2]];
    }
    return 0;
}

void lightness_map_free(LightnessMap *lm) {
    if (!lm) return;
    free(lm->data);
    lm->data = NULL;
}
