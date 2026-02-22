#ifndef GLIF_QUICKSELECT_H
#define GLIF_QUICKSELECT_H

/* NaN-safe quickselect: returns k-th smallest element of arr[0..n-1].
 * NaN values are filtered to the end. Mutates arr in-place. */
static inline float glif_quickselect(float *arr, int n, int k) {
    /* Filter NaN to end */
    int valid = n;
    for (int i = 0; i < valid; ) {
        if (arr[i] != arr[i]) { /* NaN */
            valid--;
            arr[i] = arr[valid];
        } else {
            i++;
        }
    }
    if (valid == 0) return 0.0f;
    if (k >= valid) k = valid - 1;

    int lo = 0, hi = valid - 1;
    while (lo < hi) {
        float pivot = arr[lo + (hi - lo) / 2];
        int i = lo, j = hi;
        while (i <= j) {
            while (arr[i] < pivot) i++;
            while (arr[j] > pivot) j--;
            if (i <= j) {
                float tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
                i++; j--;
            }
        }
        if (k <= j) hi = j;
        else if (k >= i) lo = i;
        else break;
    }
    return arr[k];
}

#endif
