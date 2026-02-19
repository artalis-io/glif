#ifndef GLIF_VEC6_H
#define GLIF_VEC6_H

#include <math.h>

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
    for (int i = 0; i < 6; i++) r.v[i] = a.v[i] - b.v[i];
    return r;
}

static inline GlifVec6 glif_vec6_add(GlifVec6 a, GlifVec6 b) {
    GlifVec6 r;
    for (int i = 0; i < 6; i++) r.v[i] = a.v[i] + b.v[i];
    return r;
}

static inline GlifVec6 glif_vec6_scale(GlifVec6 a, float s) {
    GlifVec6 r;
    for (int i = 0; i < 6; i++) r.v[i] = a.v[i] * s;
    return r;
}

static inline float glif_vec6_dot(GlifVec6 a, GlifVec6 b) {
    float d = 0;
    for (int i = 0; i < 6; i++) d += a.v[i] * b.v[i];
    return d;
}

static inline float glif_vec6_length(GlifVec6 a) {
    return sqrtf(glif_vec6_dot(a, a));
}

static inline float glif_vec6_dist_sq(GlifVec6 a, GlifVec6 b) {
    GlifVec6 d = glif_vec6_sub(a, b);
    return glif_vec6_dot(d, d);
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
    for (int i = 0; i < 10; i++) r.v[i] = a.v[i] - b.v[i];
    return r;
}

static inline float glif_vec10_dot(GlifVec10 a, GlifVec10 b) {
    float d = 0;
    for (int i = 0; i < 10; i++) d += a.v[i] * b.v[i];
    return d;
}

static inline float glif_vec10_length(GlifVec10 a) {
    return sqrtf(glif_vec10_dot(a, a));
}

static inline GlifVec10 glif_vec10_normalize(GlifVec10 a) {
    float len = glif_vec10_length(a);
    if (len < 1e-8f) return glif_vec10_zero();
    GlifVec10 r;
    for (int i = 0; i < 10; i++) r.v[i] = a.v[i] / len;
    return r;
}

#endif
