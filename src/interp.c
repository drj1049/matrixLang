#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "interp.h"

/* ---- A runtime matrix value: just a rows x cols grid of doubles. ---- */
typedef struct {
    double **data;
    int rows;
    int cols;
} Value;

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

static Value copy_value(const Value *src) {
    Value v = make_value(src->rows, src->cols);
    for (int r = 0; r < src->rows; r++) {
        memcpy(v.data[r], src->data[r], sizeof(double) * src->cols);
    }
    return v;
}

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

static int env_find(Env *env, const char *name) {
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->slots[i].name, name) == 0) return i;
    }
    return -1;
}

static void env_set(Env *env, const char *name, const Value *v) {
    int idx = env_find(env, name);
    if (idx == -1) {
        env->count++;
        env->slots = realloc(env->slots, sizeof(EnvSlot) * env->count);
        idx = env->count - 1;
        strncpy(env->slots[idx].name, name, TAC_NAME_MAX - 1);
        env->slots[idx].name[TAC_NAME_MAX - 1] = '\0';
    } else {
        free_value(&env->slots[idx].val);
    }
    env->slots[idx].val = copy_value(v);
}

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
 */
static Value mat_add(const Value *a, const Value *b) {
    Value out = make_value(a->rows, a->cols);
    for (int r = 0; r < a->rows; r++)
        for (int c = 0; c < a->cols; c++)
            out.data[r][c] = a->data[r][c] + b->data[r][c];
    return out;
}

static Value mat_sub(const Value *a, const Value *b) {
    Value out = make_value(a->rows, a->cols);
    for (int r = 0; r < a->rows; r++)
        for (int c = 0; c < a->cols; c++)
            out.data[r][c] = a->data[r][c] - b->data[r][c];
    return out;
}

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

static Value mat_transpose(const Value *a) {
    Value out = make_value(a->cols, a->rows);
    for (int r = 0; r < a->rows; r++)
        for (int c = 0; c < a->cols; c++)
            out.data[c][r] = a->data[r][c];
    return out;
}

/* Gauss-Jordan elimination with partial pivoting: [A | I] -> [I | A^-1] */
static Value mat_inverse(const Value *a, int line) {
    int n = a->rows;
    Value aug = make_value(n, 2 * n);
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) aug.data[r][c] = a->data[r][c];
        aug.data[r][n + r] = 1.0;
    }

    for (int col = 0; col < n; col++) {
        int pivot = col;
        for (int r = col + 1; r < n; r++) {
            if (fabs(aug.data[r][col]) > fabs(aug.data[pivot][col])) pivot = r;
        }
        if (fabs(aug.data[pivot][col]) < 1e-9) {
            fprintf(stderr, "runtime error at line %d: matrix is singular, cannot invert\n", line);
            exit(1);
        }
        if (pivot != col) {
            double *tmp = aug.data[col];
            aug.data[col] = aug.data[pivot];
            aug.data[pivot] = tmp;
        }
        double pv = aug.data[col][col];
        for (int c = 0; c < 2 * n; c++) aug.data[col][c] /= pv;
        for (int r = 0; r < n; r++) {
            if (r == col) continue;
            double factor = aug.data[r][col];
            for (int c = 0; c < 2 * n; c++) aug.data[r][c] -= factor * aug.data[col][c];
        }
    }

    Value out = make_value(n, n);
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            out.data[r][c] = aug.data[r][n + c];
    free_value(&aug);
    return out;
}

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

static void print_value(const Value *v) {
    for (int r = 0; r < v->rows; r++) {
        for (int c = 0; c < v->cols; c++) {
            printf("%8.4g ", v->data[r][c]);
        }
        printf("\n");
    }
}

void interp_run(const TACProgram *tac) {
    Env env = { NULL, 0 };

    for (int i = 0; i < tac->count; i++) {
        const TACInstr *ins = &tac->instrs[i];

        switch (ins->op) {
            case TAC_LOAD_MATRIX: {
                Value v = { ins->matrix_values, ins->rows, ins->cols };
                env_set(&env, ins->result, &v);
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
        }
    }

    for (int i = 0; i < env.count; i++) free_value(&env.slots[i].val);
    free(env.slots);
}