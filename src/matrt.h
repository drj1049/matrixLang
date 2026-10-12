#ifndef MATRT_H
#define MATRT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define BS 32  /* tile size for the blocked multiply */

typedef struct {
    int r, c;
    double *d;  /* row-major: element (i,j) is d[i*c + j] */
} Mat;

static inline Mat mat_new(int r, int c) {
    Mat m = { r, c, calloc((size_t)r * c, sizeof(double)) };
    return m;
}

static inline Mat mat_from(int r, int c, const double *vals) {
    Mat m = mat_new(r, c);
    memcpy(m.d, vals, (size_t)r * c * sizeof(double));
    return m;
}

static inline Mat mat_add(Mat a, Mat b) {
    Mat m = mat_new(a.r, a.c);
    for (int i = 0; i < a.r * a.c; i++) m.d[i] = a.d[i] + b.d[i];
    return m;
}

static inline Mat mat_sub(Mat a, Mat b) {
    Mat m = mat_new(a.r, a.c);
    for (int i = 0; i < a.r * a.c; i++) m.d[i] = a.d[i] - b.d[i];
    return m;
}

/* loop-tiled multiply: works on BS x BS chunks so they stay in cache */
static inline Mat mat_mul(Mat a, Mat b) {
    Mat m = mat_new(a.r, b.c);
    for (int ii = 0; ii < a.r; ii += BS)
        for (int kk = 0; kk < a.c; kk += BS)
            for (int jj = 0; jj < b.c; jj += BS)
                for (int i = ii; i < ii + BS && i < a.r; i++)
                    for (int k = kk; k < kk + BS && k < a.c; k++) {
                        double x = a.d[i * a.c + k];
                        for (int j = jj; j < jj + BS && j < b.c; j++)
                            m.d[i * b.c + j] += x * b.d[k * b.c + j];
                    }
    return m;
}

static inline Mat mat_transpose(Mat a) {
    Mat m = mat_new(a.c, a.r);
    for (int i = 0; i < a.r; i++)
        for (int j = 0; j < a.c; j++)
            m.d[j * a.r + i] = a.d[i * a.c + j];
    return m;
}

/* Gauss-Jordan with partial pivoting */
static inline Mat mat_inverse(Mat a) {
    int n = a.r;
    Mat m = mat_new(n, n);
    double *w = malloc((size_t)n * n * sizeof(double));
    memcpy(w, a.d, (size_t)n * n * sizeof(double));
    for (int i = 0; i < n; i++) m.d[i * n + i] = 1.0;

    for (int col = 0; col < n; col++) {
        int piv = col;
        for (int r = col + 1; r < n; r++)
            if (fabs(w[r * n + col]) > fabs(w[piv * n + col])) piv = r;
        if (fabs(w[piv * n + col]) < 1e-12) {
            fprintf(stderr, "Runtime error: matrix is singular, cannot invert\n");
            exit(1);
        }
        if (piv != col) {
            for (int k = 0; k < n; k++) {
                double t = w[piv * n + k];
                w[piv * n + k] = w[col * n + k];
                w[col * n + k] = t;
                t = m.d[piv * n + k];
                m.d[piv * n + k] = m.d[col * n + k];
                m.d[col * n + k] = t;
            }
        }
        double p = w[col * n + col];
        for (int k = 0; k < n; k++) {
            w[col * n + k] /= p;
            m.d[col * n + k] /= p;
        }
        for (int r = 0; r < n; r++) {
            if (r == col) continue;
            double f = w[r * n + col];
            for (int k = 0; k < n; k++) {
                w[r * n + k] -= f * w[col * n + k];
                m.d[r * n + k] -= f * m.d[col * n + k];
            }
        }
    }
    free(w);
    return m;
}

static inline void mat_print(const char *name, Mat a) {
    printf("%s =\n", name);
    for (int i = 0; i < a.r; i++) {
        for (int j = 0; j < a.c; j++) printf("%8.4g ", a.d[i * a.c + j]);
        printf("\n");
    }
    printf("\n");
}

#endif