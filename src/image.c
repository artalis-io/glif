#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "image.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdatomic.h>

#if defined(__EMSCRIPTEN__) && defined(__wasm_simd128__)
#include <wasm_simd128.h>
#define USE_WASM_SIMD 1
#endif

int glif_image_load(GlifImage *img, const char *path) {
    /* Force 3 channels (RGB) to avoid OOB on grayscale images (H6) */
    int orig_channels;
    img->pixels = stbi_load(path, &img->width, &img->height, &orig_channels, 3);
    if (!img->pixels) {
        fprintf(stderr, "error: failed to load image '%s': %s\n",
                path, stbi_failure_reason());
        return -1;
    }
    img->channels = 3;
    img->owns_pixels = 1;
    return 0;
}

int glif_image_load_buffer(GlifImage *img, const uint8_t *data, int w, int h, int channels) {
    if (!data || w < 1 || h < 1 || (channels != 3 && channels != 4))
        return -1;
    img->width = w;
    img->height = h;
    img->channels = channels;
    img->pixels = (uint8_t *)data;
    img->owns_pixels = 0;
    return 0;
}

void glif_image_free(GlifImage *img) {
    if (!img) return;
    if (img->pixels && img->owns_pixels) {
        stbi_image_free(img->pixels);
    }
    img->pixels = NULL;
}

/* sRGB -> linear LUT: precomputed for all 256 byte values.
 * Uses CAS to ensure exactly one thread initializes the table. */
static float srgb_lut[256];
static atomic_int srgb_lut_state = 0; /* 0=uninit, 1=in-progress, 2=ready */

static void srgb_lut_init(void) {
    int state = atomic_load_explicit(&srgb_lut_state, memory_order_acquire);
    if (state == 2) return; /* fast path: already initialized */

    int expected = 0;
    if (atomic_compare_exchange_strong_explicit(&srgb_lut_state, &expected, 1,
                                                memory_order_acq_rel,
                                                memory_order_acquire)) {
        /* Won the init race — compute LUT */
        for (int i = 0; i < 256; i++) {
            float c = (float)i / 255.0f;
            if (c <= 0.04045f)
                srgb_lut[i] = c / 12.92f;
            else
                srgb_lut[i] = powf((c + 0.055f) / 1.055f, 2.4f);
        }
        atomic_store_explicit(&srgb_lut_state, 2, memory_order_release);
    } else {
        /* Another thread is initializing — spin until done */
        while (atomic_load_explicit(&srgb_lut_state, memory_order_acquire) != 2)
            ;
    }
}

/* Recompute lightness values in-place (caller must provide pre-allocated lm->data).
 * Used in video mode to avoid per-frame malloc/free. */
int glif_lightness_map_update(GlifLightnessMap *lm, const GlifImage *img) {
    srgb_lut_init();

    size_t npixels = (size_t)img->width * (size_t)img->height;
    const uint8_t *pixels = img->pixels;
    int channels = img->channels;
    float *dest = lm->data;

#ifdef _OPENMP
    #pragma omp parallel for schedule(static)
#endif
#if USE_WASM_SIMD
    {
        v128_t wr = wasm_f32x4_splat(0.2126f);
        v128_t wg = wasm_f32x4_splat(0.7152f);
        v128_t wb = wasm_f32x4_splat(0.0722f);
        size_t i = 0, n4 = npixels & ~(size_t)3;
        for (; i < n4; i += 4) {
            const uint8_t *p0 = pixels + i * channels;
            const uint8_t *p1 = p0 + channels;
            const uint8_t *p2 = p1 + channels;
            const uint8_t *p3 = p2 + channels;
            v128_t r = wasm_f32x4_make(srgb_lut[p0[0]], srgb_lut[p1[0]],
                                       srgb_lut[p2[0]], srgb_lut[p3[0]]);
            v128_t g = wasm_f32x4_make(srgb_lut[p0[1]], srgb_lut[p1[1]],
                                       srgb_lut[p2[1]], srgb_lut[p3[1]]);
            v128_t b = wasm_f32x4_make(srgb_lut[p0[2]], srgb_lut[p1[2]],
                                       srgb_lut[p2[2]], srgb_lut[p3[2]]);
            v128_t lum = wasm_f32x4_add(wasm_f32x4_add(wasm_f32x4_mul(r, wr),
                                                         wasm_f32x4_mul(g, wg)),
                                         wasm_f32x4_mul(b, wb));
            wasm_v128_store(dest + i, lum);
        }
        for (; i < npixels; i++) {
            const uint8_t *px = pixels + i * channels;
            dest[i] = 0.2126f * srgb_lut[px[0]]
                    + 0.7152f * srgb_lut[px[1]]
                    + 0.0722f * srgb_lut[px[2]];
        }
    }
#else
    for (size_t i = 0; i < npixels; i++) {
        const uint8_t *px = pixels + i * channels;
        dest[i] = 0.2126f * srgb_lut[px[0]]
                + 0.7152f * srgb_lut[px[1]]
                + 0.0722f * srgb_lut[px[2]];
    }
#endif
    return 0;
}

int glif_lightness_map_create(GlifLightnessMap *lm, const GlifImage *img) {
    srgb_lut_init();

    lm->width = img->width;
    lm->height = img->height;
    size_t npix = (size_t)lm->width * (size_t)lm->height;
    if (npix > SIZE_MAX / sizeof(float)) return -1;
    lm->data = malloc(npix * sizeof(float));
    if (!lm->data) return -1;

    const uint8_t *pixels = img->pixels;
    int channels = img->channels;
    float *dest = lm->data;

#ifdef _OPENMP
    #pragma omp parallel for schedule(static)
#endif
#if USE_WASM_SIMD
    {
        v128_t wr = wasm_f32x4_splat(0.2126f);
        v128_t wg = wasm_f32x4_splat(0.7152f);
        v128_t wb = wasm_f32x4_splat(0.0722f);
        size_t i = 0, n4 = npix & ~(size_t)3;
        for (; i < n4; i += 4) {
            const uint8_t *p0 = pixels + i * channels;
            const uint8_t *p1 = p0 + channels;
            const uint8_t *p2 = p1 + channels;
            const uint8_t *p3 = p2 + channels;
            v128_t r = wasm_f32x4_make(srgb_lut[p0[0]], srgb_lut[p1[0]],
                                       srgb_lut[p2[0]], srgb_lut[p3[0]]);
            v128_t g = wasm_f32x4_make(srgb_lut[p0[1]], srgb_lut[p1[1]],
                                       srgb_lut[p2[1]], srgb_lut[p3[1]]);
            v128_t b = wasm_f32x4_make(srgb_lut[p0[2]], srgb_lut[p1[2]],
                                       srgb_lut[p2[2]], srgb_lut[p3[2]]);
            v128_t lum = wasm_f32x4_add(wasm_f32x4_add(wasm_f32x4_mul(r, wr),
                                                         wasm_f32x4_mul(g, wg)),
                                         wasm_f32x4_mul(b, wb));
            wasm_v128_store(dest + i, lum);
        }
        for (; i < npix; i++) {
            const uint8_t *px = pixels + i * channels;
            dest[i] = 0.2126f * srgb_lut[px[0]]
                    + 0.7152f * srgb_lut[px[1]]
                    + 0.0722f * srgb_lut[px[2]];
        }
    }
#else
    for (size_t i = 0; i < npix; i++) {
        const uint8_t *px = pixels + i * channels;
        dest[i] = 0.2126f * srgb_lut[px[0]]
                + 0.7152f * srgb_lut[px[1]]
                + 0.0722f * srgb_lut[px[2]];
    }
#endif
    return 0;
}

static int float_cmp(const void *a, const void *b) {
    float fa = *(const float *)a, fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

void glif_lightness_map_normalize(GlifLightnessMap *lm) {
    size_t n = (size_t)lm->width * (size_t)lm->height;
    if (n < 2) return;

    /* Compute p5 and p95 from a sorted copy */
    if (n > SIZE_MAX / sizeof(float)) return;
    float *sorted = malloc(n * sizeof(float));
    if (!sorted) return;

    for (size_t i = 0; i < n; i++)
        sorted[i] = lm->data[i];

    qsort(sorted, n, sizeof(float), float_cmp);

    float p5  = sorted[n / 20];        /* 5th percentile */
    float p95 = sorted[n - 1 - n / 20]; /* 95th percentile */
    free(sorted);

    float range = p95 - p5;
    if (range < 1e-4f) return; /* near-uniform frame, skip */

    float inv_range = 1.0f / range;

#ifdef _OPENMP
    #pragma omp parallel for schedule(static)
#endif
#if USE_WASM_SIMD
    {
        v128_t vp5 = wasm_f32x4_splat(p5);
        v128_t vinv = wasm_f32x4_splat(inv_range);
        v128_t vzero = wasm_f32x4_splat(0.0f);
        v128_t vone = wasm_f32x4_splat(1.0f);
        size_t i = 0, n4 = n & ~(size_t)3;
        for (; i < n4; i += 4) {
            v128_t v = wasm_v128_load(lm->data + i);
            v = wasm_f32x4_mul(wasm_f32x4_sub(v, vp5), vinv);
            v = wasm_f32x4_max(v, vzero);
            v = wasm_f32x4_min(v, vone);
            wasm_v128_store(lm->data + i, v);
        }
        for (; i < n; i++) {
            float v = (lm->data[i] - p5) * inv_range;
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            lm->data[i] = v;
        }
    }
#else
    for (size_t i = 0; i < n; i++) {
        float v = (lm->data[i] - p5) * inv_range;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        lm->data[i] = v;
    }
#endif
}

void glif_lightness_map_free(GlifLightnessMap *lm) {
    if (!lm) return;
    free(lm->data);
    lm->data = NULL;
}
