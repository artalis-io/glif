/*
 * Generate test PNG images for glif.
 * Build: cc -O2 -Ivendor -o gen_test_images tools/gen_test_images.c -lm
 * Run:   ./gen_test_images
 */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define W 320
#define H 240

static void write_gradient(const char *path) {
    unsigned char *px = malloc(W * H * 3);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int i = (y * W + x) * 3;
            px[i + 0] = (unsigned char)(x * 255 / W);
            px[i + 1] = (unsigned char)(y * 255 / H);
            px[i + 2] = (unsigned char)(128);
        }
    }
    stbi_write_png(path, W, H, 3, px, W * 3);
    free(px);
    printf("Wrote %s\n", path);
}

static void write_checkerboard(const char *path) {
    unsigned char *px = malloc(W * H * 3);
    int sq = 20;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int i = (y * W + x) * 3;
            int check = ((x / sq) + (y / sq)) % 2;
            unsigned char v = check ? 255 : 0;
            px[i + 0] = v;
            px[i + 1] = v;
            px[i + 2] = v;
        }
    }
    stbi_write_png(path, W, H, 3, px, W * 3);
    free(px);
    printf("Wrote %s\n", path);
}

static void write_shapes(const char *path) {
    unsigned char *px = calloc(W * H * 3, 1);

    /* White circle */
    int cx1 = W / 4, cy1 = H / 2, r1 = 60;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int dx = x - cx1, dy = y - cy1;
            if (dx * dx + dy * dy < r1 * r1) {
                int i = (y * W + x) * 3;
                px[i + 0] = 255;
                px[i + 1] = 255;
                px[i + 2] = 255;
            }
        }
    }

    /* Red rectangle */
    for (int y = H / 4; y < 3 * H / 4; y++) {
        for (int x = W / 2; x < W / 2 + 100; x++) {
            if (x < W && y < H) {
                int i = (y * W + x) * 3;
                px[i + 0] = 220;
                px[i + 1] = 50;
                px[i + 2] = 50;
            }
        }
    }

    /* Diagonal white line */
    for (int t = 0; t < 200; t++) {
        int x = W / 2 + 60 + t;
        int y = H / 6 + t * H / 300;
        if (x >= 0 && x < W && y >= 0 && y < H) {
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < W && ny >= 0 && ny < H) {
                        int i = (ny * W + nx) * 3;
                        px[i + 0] = 255;
                        px[i + 1] = 255;
                        px[i + 2] = 255;
                    }
                }
            }
        }
    }

    stbi_write_png(path, W, H, 3, px, W * 3);
    free(px);
    printf("Wrote %s\n", path);
}

static void write_stripes(const char *path) {
    unsigned char *px = malloc(W * H * 3);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int i = (y * W + x) * 3;
            /* Vertical color stripes with smooth sine transition */
            float t = (float)x / W * 6.2831853f * 3.0f;
            unsigned char r = (unsigned char)((sinf(t) * 0.5f + 0.5f) * 255);
            unsigned char g = (unsigned char)((sinf(t + 2.094f) * 0.5f + 0.5f) * 255);
            unsigned char b = (unsigned char)((sinf(t + 4.189f) * 0.5f + 0.5f) * 255);
            px[i + 0] = r;
            px[i + 1] = g;
            px[i + 2] = b;
        }
    }
    stbi_write_png(path, W, H, 3, px, W * 3);
    free(px);
    printf("Wrote %s\n", path);
}

int main(void) {
    write_gradient("images/gradient.png");
    write_checkerboard("images/checkerboard.png");
    write_shapes("images/shapes.png");
    write_stripes("images/stripes.png");
    return 0;
}
