#ifndef GLIF_VEC6_H
#define GLIF_VEC6_H

#include <math.h>

#if defined(__wasm_simd128__)
#include <wasm_simd128.h>
#endif

typedef struct {
    float v[6];
} GlifVec6;

typedef struct {
    float v[10];
} GlifVec10;

static inline GlifVec6 glif_vec6_zero(void) {
    return (GlifVec6){{0, 0, 0, 0, 0, 0}};
}

static inline GlifVec6 glif_vec6_sub(GlifVec6 a, GlifVec6 b) {
    GlifVec6 r;
#if defined(__wasm_simd128__)
    v128_t va = wasm_v128_load(a.v);
    v128_t vb = wasm_v128_load(b.v);
    wasm_v128_store(r.v, wasm_f32x4_sub(va, vb));
    r.v[4] = a.v[4] - b.v[4];
    r.v[5] = a.v[5] - b.v[5];
#else
    for (int i = 0; i < 6; i++) r.v[i] = a.v[i] - b.v[i];
#endif
    return r;
}

static inline GlifVec6 glif_vec6_add(GlifVec6 a, GlifVec6 b) {
    GlifVec6 r;
#if defined(__wasm_simd128__)
    v128_t va = wasm_v128_load(a.v);
    v128_t vb = wasm_v128_load(b.v);
    wasm_v128_store(r.v, wasm_f32x4_add(va, vb));
    r.v[4] = a.v[4] + b.v[4];
    r.v[5] = a.v[5] + b.v[5];
#else
    for (int i = 0; i < 6; i++) r.v[i] = a.v[i] + b.v[i];
#endif
    return r;
}

static inline GlifVec6 glif_vec6_scale(GlifVec6 a, float s) {
    GlifVec6 r;
#if defined(__wasm_simd128__)
    v128_t va = wasm_v128_load(a.v);
    v128_t vs = wasm_f32x4_splat(s);
    wasm_v128_store(r.v, wasm_f32x4_mul(va, vs));
    r.v[4] = a.v[4] * s;
    r.v[5] = a.v[5] * s;
#else
    for (int i = 0; i < 6; i++) r.v[i] = a.v[i] * s;
#endif
    return r;
}

static inline float glif_vec6_dot(GlifVec6 a, GlifVec6 b) {
#if defined(__wasm_simd128__)
    v128_t va = wasm_v128_load(a.v);
    v128_t vb = wasm_v128_load(b.v);
    v128_t prod = wasm_f32x4_mul(va, vb);
    /* Sum the 4 lanes */
    float tmp[4];
    wasm_v128_store(tmp, prod);
    return tmp[0] + tmp[1] + tmp[2] + tmp[3]
         + a.v[4] * b.v[4] + a.v[5] * b.v[5];
#else
    float d = 0;
    for (int i = 0; i < 6; i++) d += a.v[i] * b.v[i];
    return d;
#endif
}

static inline float glif_vec6_length(GlifVec6 a) {
    return sqrtf(glif_vec6_dot(a, a));
}

static inline float glif_vec6_dist_sq(GlifVec6 a, GlifVec6 b) {
#if defined(__wasm_simd128__)
    v128_t va = wasm_v128_load(a.v);
    v128_t vb = wasm_v128_load(b.v);
    v128_t d = wasm_f32x4_sub(va, vb);
    v128_t sq = wasm_f32x4_mul(d, d);
    float tmp[4];
    wasm_v128_store(tmp, sq);
    float d4 = a.v[4] - b.v[4], d5 = a.v[5] - b.v[5];
    return tmp[0] + tmp[1] + tmp[2] + tmp[3] + d4 * d4 + d5 * d5;
#else
    GlifVec6 d = glif_vec6_sub(a, b);
    return glif_vec6_dot(d, d);
#endif
}

static inline GlifVec6 glif_vec6_normalize(GlifVec6 a) {
    float len = glif_vec6_length(a);
    if (len < 1e-8f) return glif_vec6_zero();
    return glif_vec6_scale(a, 1.0f / len);
}

static inline float glif_vec6_max_component(GlifVec6 a) {
    float m = a.v[0];
    for (int i = 1; i < 6; i++)
        if (a.v[i] > m) m = a.v[i];
    return m;
}

static inline GlifVec10 glif_vec10_zero(void) {
    return (GlifVec10){{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
}

static inline GlifVec10 glif_vec10_sub(GlifVec10 a, GlifVec10 b) {
    GlifVec10 r;
#if defined(__wasm_simd128__)
    v128_t va0 = wasm_v128_load(a.v);
    v128_t vb0 = wasm_v128_load(b.v);
    wasm_v128_store(r.v, wasm_f32x4_sub(va0, vb0));
    v128_t va1 = wasm_v128_load(a.v + 4);
    v128_t vb1 = wasm_v128_load(b.v + 4);
    wasm_v128_store(r.v + 4, wasm_f32x4_sub(va1, vb1));
    r.v[8] = a.v[8] - b.v[8];
    r.v[9] = a.v[9] - b.v[9];
#else
    for (int i = 0; i < 10; i++) r.v[i] = a.v[i] - b.v[i];
#endif
    return r;
}

static inline float glif_vec10_dot(GlifVec10 a, GlifVec10 b) {
#if defined(__wasm_simd128__)
    v128_t va0 = wasm_v128_load(a.v);
    v128_t vb0 = wasm_v128_load(b.v);
    v128_t p0 = wasm_f32x4_mul(va0, vb0);
    v128_t va1 = wasm_v128_load(a.v + 4);
    v128_t vb1 = wasm_v128_load(b.v + 4);
    v128_t p1 = wasm_f32x4_mul(va1, vb1);
    v128_t sum = wasm_f32x4_add(p0, p1);
    float tmp[4];
    wasm_v128_store(tmp, sum);
    return tmp[0] + tmp[1] + tmp[2] + tmp[3]
         + a.v[8] * b.v[8] + a.v[9] * b.v[9];
#else
    float d = 0;
    for (int i = 0; i < 10; i++) d += a.v[i] * b.v[i];
    return d;
#endif
}

static inline float glif_vec10_length(GlifVec10 a) {
    return sqrtf(glif_vec10_dot(a, a));
}

static inline GlifVec10 glif_vec10_normalize(GlifVec10 a) {
    float len = glif_vec10_length(a);
    if (len < 1e-8f) return glif_vec10_zero();
    GlifVec10 r;
#if defined(__wasm_simd128__)
    v128_t inv = wasm_f32x4_splat(1.0f / len);
    wasm_v128_store(r.v, wasm_f32x4_mul(wasm_v128_load(a.v), inv));
    wasm_v128_store(r.v + 4, wasm_f32x4_mul(wasm_v128_load(a.v + 4), inv));
    r.v[8] = a.v[8] / len;
    r.v[9] = a.v[9] / len;
#else
    for (int i = 0; i < 10; i++) r.v[i] = a.v[i] / len;
#endif
    return r;
}

#endif
