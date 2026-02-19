#include "utest.h"
#include "image.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

UTEST(image, load_valid) {
    GlifImage img;
    int ret = glif_image_load(&img, "images/gradient.png");
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(img.width, 320);
    ASSERT_EQ(img.height, 240);
    ASSERT_GE(img.channels, 3);
    ASSERT_TRUE(img.pixels != NULL);
    glif_image_free(&img);
}

UTEST(image, load_nonexistent) {
    GlifImage img;
    int ret = glif_image_load(&img, "nonexistent.png");
    ASSERT_NE(ret, 0);
}

UTEST(image, lightness_map_dimensions) {
    GlifImage img;
    ASSERT_EQ(glif_image_load(&img, "images/gradient.png"), 0);

    GlifLightnessMap lm;
    ASSERT_EQ(glif_lightness_map_create(&lm, &img), 0);
    ASSERT_EQ(lm.width, img.width);
    ASSERT_EQ(lm.height, img.height);
    ASSERT_TRUE(lm.data != NULL);

    glif_lightness_map_free(&lm);
    glif_image_free(&img);
}

UTEST(image, lightness_map_range) {
    GlifImage img;
    ASSERT_EQ(glif_image_load(&img, "images/gradient.png"), 0);

    GlifLightnessMap lm;
    ASSERT_EQ(glif_lightness_map_create(&lm, &img), 0);

    /* All values should be in [0, 1] */
    for (int i = 0; i < lm.width * lm.height; i++) {
        ASSERT_GE(lm.data[i], 0.0f);
        ASSERT_LE(lm.data[i], 1.0f);
    }

    glif_lightness_map_free(&lm);
    glif_image_free(&img);
}

UTEST(image, lightness_checkerboard) {
    GlifImage img;
    ASSERT_EQ(glif_image_load(&img, "images/checkerboard.png"), 0);

    GlifLightnessMap lm;
    ASSERT_EQ(glif_lightness_map_create(&lm, &img), 0);

    /* Top-left pixel (0,0) should be black or white depending on checkerboard */
    float corner = lm.data[0];
    ASSERT_TRUE(corner < 0.01f || corner > 0.99f);

    glif_lightness_map_free(&lm);
    glif_image_free(&img);
}

UTEST(image, lightness_pure_white) {
    /* Create a synthetic pure white image (255, 255, 255) */
    GlifImage img;
    img.width = 4;
    img.height = 4;
    img.channels = 3;
    img.pixels = malloc((size_t)(4 * 4 * 3));
    ASSERT_TRUE(img.pixels != NULL);
    memset(img.pixels, 255, (size_t)(4 * 4 * 3));

    GlifLightnessMap lm;
    ASSERT_EQ(glif_lightness_map_create(&lm, &img), 0);

    /* Pure white should have luminance ~1.0 */
    for (int i = 0; i < lm.width * lm.height; i++) {
        ASSERT_NEAR(lm.data[i], 1.0f, 0.01f);
    }

    glif_lightness_map_free(&lm);
    free(img.pixels);
}

UTEST(image, lightness_pure_black) {
    /* Create a synthetic pure black image (0, 0, 0) */
    GlifImage img;
    img.width = 4;
    img.height = 4;
    img.channels = 3;
    img.pixels = calloc((size_t)(4 * 4 * 3), 1);
    ASSERT_TRUE(img.pixels != NULL);

    GlifLightnessMap lm;
    ASSERT_EQ(glif_lightness_map_create(&lm, &img), 0);

    /* Pure black should have luminance ~0.0 */
    for (int i = 0; i < lm.width * lm.height; i++) {
        ASSERT_NEAR(lm.data[i], 0.0f, 0.001f);
    }

    glif_lightness_map_free(&lm);
    free(img.pixels);
}

UTEST(image, lightness_pure_red) {
    /* Create a synthetic pure red image (255, 0, 0) */
    GlifImage img;
    img.width = 2;
    img.height = 2;
    img.channels = 3;
    img.pixels = malloc((size_t)(2 * 2 * 3));
    ASSERT_TRUE(img.pixels != NULL);
    for (int i = 0; i < 2 * 2; i++) {
        img.pixels[i * 3 + 0] = 255;  /* R */
        img.pixels[i * 3 + 1] = 0;    /* G */
        img.pixels[i * 3 + 2] = 0;    /* B */
    }

    GlifLightnessMap lm;
    ASSERT_EQ(glif_lightness_map_create(&lm, &img), 0);

    /* Pure red luminance = 0.2126 * 1.0 + 0.7152 * 0.0 + 0.0722 * 0.0 = 0.2126 */
    for (int i = 0; i < lm.width * lm.height; i++) {
        ASSERT_NEAR(lm.data[i], 0.2126f, 0.01f);
    }

    glif_lightness_map_free(&lm);
    free(img.pixels);
}

UTEST(image, lightness_pure_green) {
    /* Create a synthetic pure green image (0, 255, 0) */
    GlifImage img;
    img.width = 2;
    img.height = 2;
    img.channels = 3;
    img.pixels = malloc((size_t)(2 * 2 * 3));
    ASSERT_TRUE(img.pixels != NULL);
    for (int i = 0; i < 2 * 2; i++) {
        img.pixels[i * 3 + 0] = 0;    /* R */
        img.pixels[i * 3 + 1] = 255;  /* G */
        img.pixels[i * 3 + 2] = 0;    /* B */
    }

    GlifLightnessMap lm;
    ASSERT_EQ(glif_lightness_map_create(&lm, &img), 0);

    /* Pure green luminance = 0.7152 */
    for (int i = 0; i < lm.width * lm.height; i++) {
        ASSERT_NEAR(lm.data[i], 0.7152f, 0.01f);
    }

    glif_lightness_map_free(&lm);
    free(img.pixels);
}

UTEST(image, load_buffer_rgb) {
    /* Create a 4x4 RGB buffer */
    uint8_t pixels[4 * 4 * 3];
    memset(pixels, 128, sizeof(pixels));

    GlifImage img;
    ASSERT_EQ(glif_image_load_buffer(&img, pixels, 4, 4, 3), 0);
    ASSERT_EQ(img.width, 4);
    ASSERT_EQ(img.height, 4);
    ASSERT_EQ(img.channels, 3);
    ASSERT_EQ(img.owns_pixels, 0);
    ASSERT_TRUE(img.pixels == pixels);

    /* Lightness map should work on buffer-loaded images */
    GlifLightnessMap lm;
    ASSERT_EQ(glif_lightness_map_create(&lm, &img), 0);
    for (int i = 0; i < lm.width * lm.height; i++) {
        ASSERT_GE(lm.data[i], 0.0f);
        ASSERT_LE(lm.data[i], 1.0f);
    }
    glif_lightness_map_free(&lm);

    /* glif_image_free should not crash (does not free external buffer) */
    glif_image_free(&img);
    ASSERT_TRUE(img.pixels == NULL);
}

UTEST(image, load_buffer_rgba) {
    uint8_t pixels[2 * 2 * 4];
    for (int i = 0; i < 2 * 2; i++) {
        pixels[i * 4 + 0] = 255;  /* R */
        pixels[i * 4 + 1] = 0;    /* G */
        pixels[i * 4 + 2] = 0;    /* B */
        pixels[i * 4 + 3] = 255;  /* A */
    }

    GlifImage img;
    ASSERT_EQ(glif_image_load_buffer(&img, pixels, 2, 2, 4), 0);
    ASSERT_EQ(img.channels, 4);

    GlifLightnessMap lm;
    ASSERT_EQ(glif_lightness_map_create(&lm, &img), 0);
    /* Pure red luminance = 0.2126 (same as RGB test) */
    for (int i = 0; i < lm.width * lm.height; i++) {
        ASSERT_NEAR(lm.data[i], 0.2126f, 0.01f);
    }
    glif_lightness_map_free(&lm);
    glif_image_free(&img);
}

UTEST(image, load_buffer_rejects_invalid) {
    uint8_t pixels[12];
    GlifImage img;
    ASSERT_NE(glif_image_load_buffer(&img, NULL, 2, 2, 3), 0);
    ASSERT_NE(glif_image_load_buffer(&img, pixels, 0, 2, 3), 0);
    ASSERT_NE(glif_image_load_buffer(&img, pixels, 2, 2, 2), 0);
}

UTEST(image, free_nullifies_pointer) {
    GlifImage img;
    ASSERT_EQ(glif_image_load(&img, "images/gradient.png"), 0);
    glif_image_free(&img);
    ASSERT_TRUE(img.pixels == NULL);
}

UTEST(image, lightness_map_free_nullifies) {
    GlifImage img;
    ASSERT_EQ(glif_image_load(&img, "images/gradient.png"), 0);

    GlifLightnessMap lm;
    ASSERT_EQ(glif_lightness_map_create(&lm, &img), 0);
    glif_lightness_map_free(&lm);
    ASSERT_TRUE(lm.data == NULL);

    glif_image_free(&img);
}

UTEST_MAIN();
