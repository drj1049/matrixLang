#include <stdio.h>
#include "sample5_func.h"

int main() {
    double a[] = {1, 2, 3, 4, 5, 6};
    double b[] = {7, 8, 9, 10, 11, 12};
    double c[] = {1, 2, 3, 4};
    Mat A = {2, 3, a}, B = {3, 2, b}, C = {2, 2, c};

    Mat R = matlang_compute(A, B, C);
    mat_print("R", R);
    mat_free(R);
    return 0;
}