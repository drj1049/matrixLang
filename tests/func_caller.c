/* Calls the functions generated from sample5_func.matlang,
 * sample6_alias.matlang, sample12_gen_func.matlang and sample15_scale_func.matlang the way a real project would: through the
 * generated headers, many times, with its own arrays.
 * usage: func_caller            check the results
 *        func_caller badsize    pass a wrong-sized matrix (must exit 1)
 */
#include <stdio.h>
#include <math.h>
#include "sample5_func.h"
#include "sample6_alias.h"
#include "sample12_gen_func.h"
#include "sample15_scale_func.h"

int main(int argc, char **argv) {
    double a[] = {1, 2, 3, 4, 5, 6};
    double b[] = {7, 8, 9, 10, 11, 12};
    double c[] = {1, 2, 3, 4};
    Mat A = {2, 3, a}, B = {3, 2, b}, C = {2, 2, c};

    if (argc > 1) {
        Mat wrong = {3, 3, a};
        matlang_compute(wrong, B, C);
        return 0; /* not reached: the size check exits */
    }

    /* R = transpose(A*B*C + C), worked out by hand:
     * A*B = [58 64; 139 154], A*B*C = [250 372; 601 894] */
    double want[] = {251, 604, 374, 898};

    for (int call = 0; call < 1000; call++) {
        Mat R = matlang_compute(A, B, C);
        if (R.r != 2 || R.c != 2) { printf("wrong result size\n"); return 1; }
        for (int i = 0; i < 4; i++) {
            if (fabs(R.d[i] - want[i]) > 1e-9) {
                printf("wrong value at %d: got %g, want %g\n", i, R.d[i], want[i]);
                return 1;
            }
        }
        mat_free(R);
    }
    if (a[0] != 1 || a[5] != 6 || b[0] != 7 || c[3] != 4) {
        printf("inputs were modified\n");
        return 1;
    }

    Mat copy = alias_copy(C);
    if (copy.d == C.d) { printf("alias_copy returned the caller's own data\n"); return 1; }
    for (int i = 0; i < 4; i++) {
        if (copy.d[i] != c[i]) { printf("alias_copy returned wrong values\n"); return 1; }
    }
    mat_free(copy); /* would crash if it pointed at the stack array c */

    /* gen_func computes I - A with a negative literal, identity and zeros */
    Mat G = gen_func(C);
    double want_g[] = {0, -2, -3, -3};
    if (G.r != 2 || G.c != 2) { printf("gen_func returned the wrong size\n"); return 1; }
    for (int i = 0; i < 4; i++) {
        if (G.d[i] != want_g[i]) {
            printf("gen_func wrong value at %d: got %g, want %g\n", i, G.d[i], want_g[i]);
            return 1;
        }
    }
    mat_free(G);

    /* scale_func computes -A + 2 * A * 3, which is 5 * A */
    Mat S = scale_func(C);
    double want_s[] = {5, 10, 15, 20};
    if (S.r != 2 || S.c != 2) { printf("scale_func returned the wrong size\n"); return 1; }
    for (int i = 0; i < 4; i++) {
        if (S.d[i] != want_s[i]) {
            printf("scale_func wrong value at %d: got %g, want %g\n", i, S.d[i], want_s[i]);
            return 1;
        }
    }
    mat_free(S);

    return 0;
}
