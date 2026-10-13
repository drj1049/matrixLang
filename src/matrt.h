/*
 * matrt.h : the tiny runtime library that generated C programs use.
 *
 * "Runtime" = code that is needed while the compiled program RUNS. The
 * generated C file only contains one line per TAC instruction (for example
 * mat_set(&v_C, mat_mul(v_A, v_B))), and this header supplies the matrix type
 * and all the functions those lines call. It is a single header, so the
 * generated code compiles with plain `gcc file.c` and nothing to link.
 *
 * Every function is `static inline` so a header only library does not cause
 * "defined twice" errors when several files include it.
 *
 * OWNERSHIP RULE: a Mat owns its `d` array. Functions that build a matrix
 * (mat_add, mat_mul, ...) return a NEW Mat and never change their arguments.
 */
#ifndef MATRT_H
#define MATRT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define BS 32  /* tile size for the blocked multiply */

/* A matrix: its size and ONE flat array of numbers, row after row. */
typedef struct {
    int r, c;
    double *d;  /* row-major: element (i,j) is d[i*c + j] */
} Mat;

/* A new r x c matrix filled with zeros. Stops the program if memory runs out. */
static inline Mat mat_new(int r, int c) {
    Mat m = { r, c, (double *)calloc((size_t)r * c, sizeof(double)) };
    if (!m.d) {
        fprintf(stderr, "Runtime error: out of memory for a %dx%d matrix\n", r, c);
        exit(1);
    }
    return m;
}

/* A new matrix holding a copy of r*c numbers given in row-major order. */
static inline Mat mat_from(int r, int c, const double *vals) {
    Mat m = mat_new(r, c);
    memcpy(m.d, vals, (size_t)r * c * sizeof(double));
    return m;
}

/* The n x n identity matrix: ones on the diagonal, zeros elsewhere. */
static inline Mat mat_identity(int n) {
    Mat m = mat_new(n, n);
    for (int i = 0; i < n; i++) m.d[i * n + i] = 1.0;
    return m;
}

/* Every element times k. */
static inline Mat mat_scale(Mat a, double k) {
    Mat m = mat_new(a.r, a.c);
    for (int i = 0; i < a.r * a.c; i++) m.d[i] = a.d[i] * k + 0.0;   /* + 0.0 turns -0 into 0 */
    return m;
}

/* An independent copy (own memory). */
static inline Mat mat_copy(Mat a) {
    return mat_from(a.r, a.c, a.d);
}

/* Gives the matrix's memory back. Call it on the result a generated function returns. */
static inline void mat_free(Mat m) {
    free(m.d);
}

/* dst = val, releasing whatever dst held before */
static inline void mat_set(Mat *dst, Mat val) {
    free(dst->d);     /* free(NULL) is fine, so a fresh "= {0}" variable works too */
    *dst = val;
}

/* dst takes over src's data without copying it */
static inline void mat_move(Mat *dst, Mat *src) {
    mat_set(dst, *src);
    src->d = NULL;    /* src no longer owns the data, so a later free of src does nothing */
}

/* generated functions call this on every input before touching it */
static inline void mat_check(const char *fn, const char *name, Mat m, int r, int c) {
    if (m.r != r || m.c != c) {
        fprintf(stderr, "%s: input %s must be %dx%d, got %dx%d\n", fn, name, r, c, m.r, m.c);
        exit(1);
    }
    if (!m.d) {
        fprintf(stderr, "%s: input %s has no data (NULL pointer)\n", fn, name);
        exit(1);
    }
}

/* element by element sum (the sizes were checked at compile time) */
static inline Mat mat_add(Mat a, Mat b) {
    Mat m = mat_new(a.r, a.c);
    for (int i = 0; i < a.r * a.c; i++) m.d[i] = a.d[i] + b.d[i];
    return m;
}

/* element by element difference */
static inline Mat mat_sub(Mat a, Mat b) {
    Mat m = mat_new(a.r, a.c);
    for (int i = 0; i < a.r * a.c; i++) m.d[i] = a.d[i] - b.d[i];
    return m;
}

/* loop-tiled multiply: works on BS x BS chunks so they stay in cache.
 * The result is the same as the plain triple loop: m[i][j] = sum of
 * a[i][k] * b[k][j]. The three outer loops (ii, kk, jj) pick a tile; the
 * three inner loops do the normal multiply, but only inside that tile, so
 * the pieces of a, b and m being used are small enough to stay in the CPU
 * cache. The order i, k, j is chosen so the innermost loop walks through
 * memory in a straight line (fast). m starts as zeros (mat_new), and every
 * tile adds its part into it with +=. */
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

/* rows become columns */
static inline Mat mat_transpose(Mat a) {
    Mat m = mat_new(a.c, a.r);
    for (int i = 0; i < a.r; i++)
        for (int j = 0; j < a.c; j++)
            m.d[j * a.r + i] = a.d[i * a.c + j];
    return m;
}

/* Gauss-Jordan with partial pivoting.
 * Same method as mat_inverse in interp.c, but it keeps two separate n x n
 * arrays instead of one wide [A | I] array: `w` is a working copy of A that
 * is turned into the identity, and `m` starts as the identity and gets every
 * row operation applied to it, so it ends up as the inverse. */
static inline Mat mat_inverse(Mat a) {
    int n = a.r;
    Mat m = mat_new(n, n);
    double *w = (double *)malloc((size_t)n * n * sizeof(double));
    memcpy(w, a.d, (size_t)n * n * sizeof(double));
    for (int i = 0; i < n; i++) m.d[i * n + i] = 1.0;

    for (int col = 0; col < n; col++) {
        /* choose the row with the biggest number in this column as the pivot */
        int piv = col;
        for (int r = col + 1; r < n; r++)
            if (fabs(w[r * n + col]) > fabs(w[piv * n + col])) piv = r;
        if (fabs(w[piv * n + col]) < 1e-12) {
            fprintf(stderr, "Runtime error: matrix is singular, cannot invert\n");
            exit(1);
        }
        if (piv != col) {
            /* swap the two rows in BOTH arrays */
            for (int k = 0; k < n; k++) {
                double t = w[piv * n + k];
                w[piv * n + k] = w[col * n + k];
                w[col * n + k] = t;
                t = m.d[piv * n + k];
                m.d[piv * n + k] = m.d[col * n + k];
                m.d[col * n + k] = t;
            }
        }
        /* scale the pivot row so the pivot becomes 1 */
        double p = w[col * n + col];
        for (int k = 0; k < n; k++) {
            w[col * n + k] /= p;
            m.d[col * n + k] /= p;
        }
        /* clear this column in every other row */
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

/* Prints "name =" and then the matrix, one row per line. */
static inline void mat_print(const char *name, Mat a) {
    printf("%s =\n", name);
    for (int i = 0; i < a.r; i++) {
        for (int j = 0; j < a.c; j++) printf("%8.4g ", a.d[i * a.c + j] + 0.0);   /* + 0.0: never print -0 */
        printf("\n");
    }
    printf("\n");
}

#endif
