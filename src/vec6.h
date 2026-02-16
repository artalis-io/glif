#ifndef VEC6_H
#define VEC6_H

#include <math.h>

typedef struct {
    float v[6];
} Vec6;

typedef struct {
    float v[10];
} Vec10;

static inline Vec6 vec6_zero(void) {
    return (Vec6){{0, 0, 0, 0, 0, 0}};
}

static inline Vec6 vec6_sub(Vec6 a, Vec6 b) {
    Vec6 r;
    for (int i = 0; i < 6; i++) r.v[i] = a.v[i] - b.v[i];
    return r;
}

static inline Vec6 vec6_add(Vec6 a, Vec6 b) {
    Vec6 r;
    for (int i = 0; i < 6; i++) r.v[i] = a.v[i] + b.v[i];
    return r;
}

static inline Vec6 vec6_scale(Vec6 a, float s) {
    Vec6 r;
    for (int i = 0; i < 6; i++) r.v[i] = a.v[i] * s;
    return r;
}

static inline float vec6_dot(Vec6 a, Vec6 b) {
    float d = 0;
    for (int i = 0; i < 6; i++) d += a.v[i] * b.v[i];
    return d;
}

static inline float vec6_length(Vec6 a) {
    return sqrtf(vec6_dot(a, a));
}

static inline float vec6_dist_sq(Vec6 a, Vec6 b) {
    Vec6 d = vec6_sub(a, b);
    return vec6_dot(d, d);
}

static inline Vec6 vec6_normalize(Vec6 a) {
    float len = vec6_length(a);
    if (len < 1e-8f) return vec6_zero();
    return vec6_scale(a, 1.0f / len);
}

static inline float vec6_max_component(Vec6 a) {
    float m = a.v[0];
    for (int i = 1; i < 6; i++)
        if (a.v[i] > m) m = a.v[i];
    return m;
}

static inline Vec10 vec10_zero(void) {
    return (Vec10){{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
}

static inline Vec10 vec10_sub(Vec10 a, Vec10 b) {
    Vec10 r;
    for (int i = 0; i < 10; i++) r.v[i] = a.v[i] - b.v[i];
    return r;
}

static inline float vec10_dot(Vec10 a, Vec10 b) {
    float d = 0;
    for (int i = 0; i < 10; i++) d += a.v[i] * b.v[i];
    return d;
}

static inline float vec10_length(Vec10 a) {
    return sqrtf(vec10_dot(a, a));
}

static inline Vec10 vec10_normalize(Vec10 a) {
    float len = vec10_length(a);
    if (len < 1e-8f) return vec10_zero();
    Vec10 r;
    for (int i = 0; i < 10; i++) r.v[i] = a.v[i] / len;
    return r;
}

#endif
