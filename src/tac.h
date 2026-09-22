#ifndef TAC_H
#define TAC_H

#include "parser.h"

/* ---- Three-Address Code ----
 * Each instruction has at most one operator and produces one result,
 * the classic TAC shape taught in compiler courses. We generate this
 * by walking the semantically-checked AST bottom-up, introducing a
 * fresh temporary (t0, t1, ...) for every intermediate value.
 *
 * Forms produced:
 *   t = LOADMATRIX RxC              (materializes a matrix literal)
 *   t = a op b                      (op is '+', '-', '*')
 *   t = func(a1, a2, ..., an)       (func is add/subtract/multiply/transpose/inverse)
 *   t = a                           (plain copy, e.g. X = Y or X = t3)
 *   PRINT a
 */
typedef enum {
    TAC_LOAD_MATRIX,
    TAC_BINOP,
    TAC_CALL,
    TAC_COPY,
    TAC_PRINT
} TACOp;

#define TAC_NAME_MAX 64
#define TAC_MAX_CALL_ARGS 16

typedef struct {
    TACOp op;
    int line;

    char result[TAC_NAME_MAX];   /* every op except PRINT writes a result */

    /* TAC_BINOP */
    char arg1[TAC_NAME_MAX];
    char arg2[TAC_NAME_MAX];
    char binop;                  /* '+', '-', '*' */

    /* TAC_CALL */
    char func_name[16];          /* "add", "subtract", "multiply", "transpose", "inverse" */
    char call_args[TAC_MAX_CALL_ARGS][TAC_NAME_MAX];
    int call_arg_count;

    /* TAC_COPY / TAC_PRINT */
    char src[TAC_NAME_MAX];

    /* TAC_LOAD_MATRIX */
    int rows;
    int cols;
    double **matrix_values;      /* owned deep copy of the literal's numbers */
} TACInstr;

typedef struct {
    TACInstr *instrs;
    int count;
} TACProgram;

/* Walks the (already semantically-checked) Program and produces a flat
 * list of TAC instructions in source order. Assumes semantic_check()
 * has already run, since it relies on the AST being well-formed
 * (no dimension errors, no undefined variables).
 */
TACProgram tac_generate(Program *prog);

/* Prints each instruction in a readable "t0 = A + B" style, one per line. */
void tac_print(const TACProgram *tac);

/* Frees the instruction array. */
void tac_free(TACProgram *tac);

#endif /* TAC_H */