/*
 * interp.c : runs a TAC program and prints the results.
 *
 * It keeps an ENVIRONMENT: a table from variable name to a matrix value.
 * Executing an instruction means: read its operands from the environment, do
 * the arithmetic, and store the result under the result name. That is all an
 * interpreter for straight-line code needs.
 *
 * MEMORY RULE used everywhere in this file: every matrix stored in the
 * environment is its own private copy (a deep copy). The arithmetic functions
 * always return a brand new matrix. So nothing is ever shared, which keeps
 * freeing memory simple and safe.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "interp.h"

/* ---- A runtime matrix value: just a rows x cols grid of doubles. ---- */
typedef struct {
    double **data;   /* data[r][c]: an array of row pointers */
    int rows;
    int cols;
} Value;

/* A new rows x cols matrix with every element 0 (calloc zero fills). */
static Value make_value(int rows, int cols) {
    Value v;
    v.rows = rows;
    v.cols = cols;
    v.data = malloc(sizeof(double *) * rows);
    for (int r = 0; r < rows; r++) {
        v.data[r] = calloc(cols, sizeof(double));
    }
    return v;
}

/* An independent copy of a matrix (new memory, same numbers). */
static Value copy_value(const Value *src) {
    Value v = make_value(src->rows, src->cols);
    for (int r = 0; r < src->rows; r++) {
        memcpy(v.data[r], src->data[r], sizeof(double) * src->cols);
    }
    return v;
}

/* Gives the memory of a matrix back. */
static void free_value(Value *v) {
    for (int r = 0; r < v->rows; r++) free(v->data[r]);
    free(v->data);
    v->data = NULL;
}

/* ---- Environment: variable name -> Value, linear-search like the symbol
 * table in semantic.c. Every slot owns a deep copy of its data.
 */
typedef struct {
    char name[TAC_NAME_MAX];
    Value val;
} EnvSlot;

typedef struct {
    EnvSlot *slots;
    int count;
} Env;

/* Index of the variable in the environment, or -1 if it is not there. */
static int env_find(Env *env, const char *name) {
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->slots[i].name, name) == 0) return i;
    }
    return -1;
}

/* Stores a COPY of v under `name` (replacing any older value of that name). */
static void env_set(Env *env, const char *name, const Value *v) {
    /* Snapshot v's data into independent memory BEFORE touching env->slots.
     * v often points *into* env->slots (e.g. a plain copy statement reads
     * the source variable's slot directly) -- growing env->slots below can
     * relocate that array, which would leave v dangling if we read from it
     * afterward. Copying first makes this safe regardless of what happens
     * to the slots array next.
     */
    Value snapshot = copy_value(v);

    int idx = env_find(env, name);
    if (idx == -1) {
        env->count++;
        env->slots = realloc(env->slots, sizeof(EnvSlot) * env->count);
        idx = env->count - 1;
        strncpy(env->slots[idx].name, name, TAC_NAME_MAX - 1);
        env->slots[idx].name[TAC_NAME_MAX - 1] = '\0';
    } else {
        free_value(&env->slots[idx].val);   /* drop the old value of this variable */
    }
    env->slots[idx].val = snapshot;
}

/* Looks up a variable's value. The pointer points into the environment, so
 * it is only valid until the next env_set. */
static Value *env_get(Env *env, const char *name) {
    int idx = env_find(env, name);
    if (idx == -1) {
        fprintf(stderr, "runtime error: '%s' used before being set (should be impossible, "
                         "semantic check should have caught this)\n", name);
        exit(1);
    }
    return &env->slots[idx].val;
}

/* ---- Matrix operations. Each returns a freshly allocated Value; caller
 * is responsible for freeing it (or handing it to env_set + freeing after,
 * same ownership pattern used throughout the compiler's other stages).
 * The sizes were already checked by semantic.c, so no size checks here.
 */

/* element by element sum */
static Value mat_add(const Value *a, const Value *b) {
    Value out = make_value(a->rows, a->cols);
    for (int r = 0; r < a->rows; r++)
        for (int c = 0; c < a->cols; c++)
            out.data[r][c] = a->data[r][c] + b->data[r][c];
    return out;
}

/* element by element difference */
static Value mat_sub(const Value *a, const Value *b) {
    Value out = make_value(a->rows, a->cols);
    for (int r = 0; r < a->rows; r++)
        for (int c = 0; c < a->cols; c++)
            out.data[r][c] = a->data[r][c] - b->data[r][c];
    return out;
}

/* matrix product: out[r][c] = sum over k of a[r][k] * b[k][c]
 * (the textbook triple loop; the generated C code uses a faster tiled version) */
static Value mat_mul(const Value *a, const Value *b) {
    Value out = make_value(a->rows, b->cols);
    for (int r = 0; r < a->rows; r++) {
        for (int c = 0; c < b->cols; c++) {
            double sum = 0;
            for (int k = 0; k < a->cols; k++) sum += a->data[r][k] * b->data[k][c];
            out.data[r][c] = sum;
        }
    }
    return out;
}

/* rows become columns */
static Value mat_transpose(const Value *a) {
    Value out = make_value(a->cols, a->rows);
    for (int r = 0; r < a->rows; r++)
        for (int c = 0; c < a->cols; c++)
            out.data[c][r] = a->data[r][c];
    return out;
}

/* Gauss-Jordan elimination with partial pivoting: [A | I] -> [I | A^-1]
 * Put the identity next to A, then use row operations to turn the left half
 * into the identity. The same operations turn the right half into the inverse.
 * "Partial pivoting" = for each column, pick the row with the biggest number
 * as the pivot, which keeps rounding errors small. If the best pivot is
 * (almost) zero, the matrix has no inverse ("singular"). */
static Value mat_inverse(const Value *a, int line) {
    int n = a->rows;
    Value aug = make_value(n, 2 * n);    /* the augmented matrix [A | I] */
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) aug.data[r][c] = a->data[r][c];
        aug.data[r][n + r] = 1.0;
    }

    for (int col = 0; col < n; col++) {
        /* find the row (from col downwards) with the largest value in this column */
        int pivot = col;
        for (int r = col + 1; r < n; r++) {
            if (fabs(aug.data[r][col]) > fabs(aug.data[pivot][col])) pivot = r;
        }
        if (fabs(aug.data[pivot][col]) < 1e-9) {
            fprintf(stderr, "runtime error at line %d: matrix is singular, cannot invert\n", line);
            exit(1);
        }
        if (pivot != col) {
            /* swapping the row pointers swaps the whole rows */
            double *tmp = aug.data[col];
            aug.data[col] = aug.data[pivot];
            aug.data[pivot] = tmp;
        }
        /* scale the pivot row so the pivot becomes 1 */
        double pv = aug.data[col][col];
        for (int c = 0; c < 2 * n; c++) aug.data[col][c] /= pv;
        /* subtract multiples of the pivot row from every other row, so the
         * rest of this column becomes 0 */
        for (int r = 0; r < n; r++) {
            if (r == col) continue;
            double factor = aug.data[r][col];
            for (int c = 0; c < 2 * n; c++) aug.data[r][c] -= factor * aug.data[col][c];
        }
    }

    /* the inverse is now the right half */
    Value out = make_value(n, n);
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            out.data[r][c] = aug.data[r][n + c];
    free_value(&aug);
    return out;
}

/* Runs a function call instruction. For add, subtract and multiply with many
 * arguments it folds from the left: add(a, b, c) = (a + b) + c. `acc` is the
 * running result; each step builds a new matrix and frees the old one. */
static Value run_call(Env *env, const TACInstr *ins) {
    Value acc = copy_value(env_get(env, ins->call_args[0]));

    if (strcmp(ins->func_name, "add") == 0) {
        for (int i = 1; i < ins->call_arg_count; i++) {
            Value next = mat_add(&acc, env_get(env, ins->call_args[i]));
            free_value(&acc);
            acc = next;
        }
    } else if (strcmp(ins->func_name, "subtract") == 0) {
        for (int i = 1; i < ins->call_arg_count; i++) {
            Value next = mat_sub(&acc, env_get(env, ins->call_args[i]));
            free_value(&acc);
            acc = next;
        }
    } else if (strcmp(ins->func_name, "multiply") == 0) {
        for (int i = 1; i < ins->call_arg_count; i++) {
            Value next = mat_mul(&acc, env_get(env, ins->call_args[i]));
            free_value(&acc);
            acc = next;
        }
    } else if (strcmp(ins->func_name, "transpose") == 0) {
        Value next = mat_transpose(&acc);
        free_value(&acc);
        acc = next;
    } else if (strcmp(ins->func_name, "inverse") == 0) {
        Value next = mat_inverse(&acc, ins->line);
        free_value(&acc);
        acc = next;
    }
    return acc;
}

/* Prints a matrix, one row per line. %8.4g = width 8, up to 4 significant digits. */
static void print_value(const Value *v) {
    for (int r = 0; r < v->rows; r++) {
        for (int c = 0; c < v->cols; c++) {
            printf("%8.4g ", v->data[r][c] + 0.0);   /* + 0.0: never print -0 */
        }
        printf("\n");
    }
}

/* The entry point (see interp.h): the main loop. One `case` for each kind of
 * instruction. */
void interp_run(const TACProgram *tac) {
    Env env = { NULL, 0 };

    for (int i = 0; i < tac->count; i++) {
        const TACInstr *ins = &tac->instrs[i];

        switch (ins->op) {
            case TAC_LOAD_MATRIX: {
                /* wrap the literal's numbers in a Value just to hand it to
                 * env_set, which makes its own copy. (Nothing is freed here,
                 * the numbers still belong to the TAC instruction.) */
                Value v = { ins->matrix_values, ins->rows, ins->cols };
                env_set(&env, ins->result, &v);
                break;
            }
            case TAC_GEN: {
                Value v = make_value(ins->rows, ins->cols);   /* starts as all zeros */
                if (strcmp(ins->func_name, "identity") == 0) {
                    for (int d = 0; d < ins->rows; d++) v.data[d][d] = 1.0;
                }
                env_set(&env, ins->result, &v);
                free_value(&v);
                break;
            }
            case TAC_SCALE: {
                Value *a = env_get(&env, ins->arg1);
                Value out = make_value(a->rows, a->cols);
                for (int r = 0; r < a->rows; r++)
                    for (int c = 0; c < a->cols; c++)
                        out.data[r][c] = a->data[r][c] * ins->scalar + 0.0;   /* + 0.0 turns -0 into 0 */
                env_set(&env, ins->result, &out);
                free_value(&out);
                break;
            }
            case TAC_BINOP: {
                Value *a = env_get(&env, ins->arg1);
                Value *b = env_get(&env, ins->arg2);
                Value out = (ins->binop == '+') ? mat_add(a, b)
                          : (ins->binop == '-') ? mat_sub(a, b)
                                                 : mat_mul(a, b);
                env_set(&env, ins->result, &out);
                free_value(&out);
                break;
            }
            case TAC_CALL: {
                Value out = run_call(&env, ins);
                env_set(&env, ins->result, &out);
                free_value(&out);
                break;
            }
            case TAC_COPY: {
                env_set(&env, ins->result, env_get(&env, ins->src));
                break;
            }
            case TAC_PRINT: {
                printf("%s =\n", ins->src);
                print_value(env_get(&env, ins->src));
                printf("\n");
                break;
            }
            case TAC_INPUT: {
                /* the numbers only exist once a C caller passes them in */
                fprintf(stderr, "runtime error at line %d: input '%s' has no data here, "
                                "compile this program to C and call it instead\n",
                        ins->line, ins->result);
                exit(1);
            }
            case TAC_OUTPUT: {
                printf("%s =\n", ins->src);
                print_value(env_get(&env, ins->src));
                printf("\n");
                break;
            }
        }
    }

    /* cleanup: free every matrix still in the environment */
    for (int i = 0; i < env.count; i++) free_value(&env.slots[i].val);
    free(env.slots);
}
