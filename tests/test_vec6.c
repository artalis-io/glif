#include "utest.h"
#include "vec6.h"
#include <math.h>

UTEST(vec6, zero) {
    Vec6 z = vec6_zero();
    for (int i = 0; i < 6; i++)
        ASSERT_NEAR(z.v[i], 0.0f, 1e-9f);
}

UTEST(vec6, add) {
    Vec6 a = {{1, 2, 3, 4, 5, 6}};
    Vec6 b = {{6, 5, 4, 3, 2, 1}};
    Vec6 c = vec6_add(a, b);
    for (int i = 0; i < 6; i++)
        ASSERT_NEAR(c.v[i], 7.0f, 1e-6f);
}

UTEST(vec6, add_identity) {
    /* Adding zero vector should return the original */
    Vec6 a = {{1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f}};
    Vec6 z = vec6_zero();
    Vec6 c = vec6_add(a, z);
    for (int i = 0; i < 6; i++)
        ASSERT_NEAR(c.v[i], a.v[i], 1e-9f);
}

UTEST(vec6, sub) {
    Vec6 a = {{10, 20, 30, 40, 50, 60}};
    Vec6 b = {{1, 2, 3, 4, 5, 6}};
    Vec6 c = vec6_sub(a, b);
    ASSERT_NEAR(c.v[0], 9.0f, 1e-6f);
    ASSERT_NEAR(c.v[5], 54.0f, 1e-6f);
}

UTEST(vec6, sub_identity) {
    /* Subtracting zero vector should return the original */
    Vec6 a = {{1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f}};
    Vec6 z = vec6_zero();
    Vec6 c = vec6_sub(a, z);
    for (int i = 0; i < 6; i++)
        ASSERT_NEAR(c.v[i], a.v[i], 1e-9f);
}

UTEST(vec6, sub_self_is_zero) {
    Vec6 a = {{3.0f, 7.0f, 1.0f, 9.0f, 2.0f, 5.0f}};
    Vec6 c = vec6_sub(a, a);
    for (int i = 0; i < 6; i++)
        ASSERT_NEAR(c.v[i], 0.0f, 1e-9f);
}

UTEST(vec6, scale) {
    Vec6 a = {{1, 2, 3, 4, 5, 6}};
    Vec6 b = vec6_scale(a, 2.0f);
    ASSERT_NEAR(b.v[0], 2.0f, 1e-6f);
    ASSERT_NEAR(b.v[5], 12.0f, 1e-6f);
}

UTEST(vec6, scale_by_zero) {
    Vec6 a = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}};
    Vec6 b = vec6_scale(a, 0.0f);
    for (int i = 0; i < 6; i++)
        ASSERT_NEAR(b.v[i], 0.0f, 1e-9f);
}

UTEST(vec6, scale_by_negative) {
    Vec6 a = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}};
    Vec6 b = vec6_scale(a, -1.0f);
    for (int i = 0; i < 6; i++)
        ASSERT_NEAR(b.v[i], -a.v[i], 1e-6f);
}

UTEST(vec6, scale_by_one) {
    Vec6 a = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}};
    Vec6 b = vec6_scale(a, 1.0f);
    for (int i = 0; i < 6; i++)
        ASSERT_NEAR(b.v[i], a.v[i], 1e-9f);
}

UTEST(vec6, dot) {
    Vec6 a = {{1, 0, 0, 0, 0, 0}};
    Vec6 b = {{1, 0, 0, 0, 0, 0}};
    ASSERT_NEAR(vec6_dot(a, b), 1.0f, 1e-6f);

    Vec6 c = {{1, 2, 3, 4, 5, 6}};
    Vec6 d = {{6, 5, 4, 3, 2, 1}};
    /* 6+10+12+12+10+6 = 56 */
    ASSERT_NEAR(vec6_dot(c, d), 56.0f, 1e-6f);
}

UTEST(vec6, dot_orthogonal) {
    /* Orthogonal vectors have zero dot product */
    Vec6 a = {{1, 0, 0, 0, 0, 0}};
    Vec6 b = {{0, 1, 0, 0, 0, 0}};
    ASSERT_NEAR(vec6_dot(a, b), 0.0f, 1e-9f);
}

UTEST(vec6, length) {
    Vec6 a = {{3, 4, 0, 0, 0, 0}};
    ASSERT_NEAR(vec6_length(a), 5.0f, 1e-5f);
}

UTEST(vec6, length_zero_vector) {
    Vec6 z = vec6_zero();
    ASSERT_NEAR(vec6_length(z), 0.0f, 1e-9f);
}

UTEST(vec6, length_unit_vector) {
    Vec6 a = {{1, 0, 0, 0, 0, 0}};
    ASSERT_NEAR(vec6_length(a), 1.0f, 1e-9f);
}

UTEST(vec6, dist_sq) {
    Vec6 a = {{1, 0, 0, 0, 0, 0}};
    Vec6 b = {{0, 0, 0, 0, 0, 0}};
    ASSERT_NEAR(vec6_dist_sq(a, b), 1.0f, 1e-6f);
}

UTEST(vec6, dist_sq_identical) {
    /* Distance between identical vectors should be 0 */
    Vec6 a = {{0.3f, 0.7f, 0.1f, 0.9f, 0.5f, 0.2f}};
    ASSERT_NEAR(vec6_dist_sq(a, a), 0.0f, 1e-9f);
}

UTEST(vec6, dist_sq_symmetric) {
    Vec6 a = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}};
    Vec6 b = {{6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f}};
    ASSERT_NEAR(vec6_dist_sq(a, b), vec6_dist_sq(b, a), 1e-9f);
}

UTEST(vec6, normalize) {
    Vec6 a = {{3, 4, 0, 0, 0, 0}};
    Vec6 n = vec6_normalize(a);
    ASSERT_NEAR(vec6_length(n), 1.0f, 1e-5f);
    ASSERT_NEAR(n.v[0], 0.6f, 1e-5f);
    ASSERT_NEAR(n.v[1], 0.8f, 1e-5f);
}

UTEST(vec6, normalize_zero) {
    Vec6 z = vec6_zero();
    Vec6 n = vec6_normalize(z);
    ASSERT_NEAR(vec6_length(n), 0.0f, 1e-9f);
}

UTEST(vec6, normalize_already_unit) {
    Vec6 a = {{1, 0, 0, 0, 0, 0}};
    Vec6 n = vec6_normalize(a);
    ASSERT_NEAR(n.v[0], 1.0f, 1e-6f);
    for (int i = 1; i < 6; i++)
        ASSERT_NEAR(n.v[i], 0.0f, 1e-6f);
}

UTEST(vec6, normalize_negative_values) {
    Vec6 a = {{-3, -4, 0, 0, 0, 0}};
    Vec6 n = vec6_normalize(a);
    ASSERT_NEAR(vec6_length(n), 1.0f, 1e-5f);
    ASSERT_NEAR(n.v[0], -0.6f, 1e-5f);
    ASSERT_NEAR(n.v[1], -0.8f, 1e-5f);
}

UTEST(vec6, max_component) {
    Vec6 a = {{0.1f, 0.5f, 0.3f, 0.9f, 0.2f, 0.4f}};
    ASSERT_NEAR(vec6_max_component(a), 0.9f, 1e-6f);
}

UTEST(vec6, max_component_first) {
    Vec6 a = {{1.0f, 0.5f, 0.3f, 0.2f, 0.1f, 0.0f}};
    ASSERT_NEAR(vec6_max_component(a), 1.0f, 1e-6f);
}

UTEST(vec6, max_component_last) {
    Vec6 a = {{0.0f, 0.1f, 0.2f, 0.3f, 0.5f, 1.0f}};
    ASSERT_NEAR(vec6_max_component(a), 1.0f, 1e-6f);
}

UTEST(vec6, max_component_negative) {
    Vec6 a = {{-5.0f, -4.0f, -3.0f, -2.0f, -1.0f, -0.5f}};
    ASSERT_NEAR(vec6_max_component(a), -0.5f, 1e-6f);
}

UTEST(vec10, zero) {
    Vec10 z = vec10_zero();
    for (int i = 0; i < 10; i++)
        ASSERT_NEAR(z.v[i], 0.0f, 1e-9f);
}

UTEST(vec10, sub) {
    Vec10 a = {{10, 20, 30, 40, 50, 60, 70, 80, 90, 100}};
    Vec10 b = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}};
    Vec10 c = vec10_sub(a, b);
    ASSERT_NEAR(c.v[0], 9.0f, 1e-6f);
    ASSERT_NEAR(c.v[9], 90.0f, 1e-6f);
}

UTEST(vec10, normalize) {
    Vec10 a = {{3, 4, 0, 0, 0, 0, 0, 0, 0, 0}};
    Vec10 n = vec10_normalize(a);
    ASSERT_NEAR(vec10_length(n), 1.0f, 1e-5f);
}

UTEST(vec10, length_zero_vector) {
    Vec10 z = vec10_zero();
    ASSERT_NEAR(vec10_length(z), 0.0f, 1e-9f);
}

UTEST(vec10, normalize_zero) {
    Vec10 z = vec10_zero();
    Vec10 n = vec10_normalize(z);
    ASSERT_NEAR(vec10_length(n), 0.0f, 1e-9f);
}

UTEST(vec10, dot_orthogonal) {
    Vec10 a = {{1, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    Vec10 b = {{0, 1, 0, 0, 0, 0, 0, 0, 0, 0}};
    ASSERT_NEAR(vec10_dot(a, b), 0.0f, 1e-9f);
}

UTEST(vec10, dot_self_equals_length_sq) {
    Vec10 a = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}};
    float dot = vec10_dot(a, a);
    float len = vec10_length(a);
    ASSERT_NEAR(dot, len * len, 1e-3f);
}

UTEST_MAIN();
