/*
 * tac.h : Three-Address Code (TAC), the compiler's intermediate language.
 *
 * WHY HAVE AN INTERMEDIATE LANGUAGE?
 * A tree is good for checking a program, but awkward to run or to turn into
 * C. TAC flattens the tree into a simple list of small steps, where every
 * step does ONE thing. For example   D = A * B + C   becomes
 *
 *     t0 = A * B
 *     t1 = t0 + C
 *     D  = t1
 *
 * Such a flat list is easy to optimize (tacopt.c), to run (interp.c) and to
 * translate to C (codegen.c). The interpreter and the C generator both
 * read exactly this list, so they always agree with each other.
 */
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
 *   t = identity(n) / t = zeros(r, c)
 *   t = k * a                       (every element of a times the number k)
 *   t = a op b                      (op is '+', '-', '*')
 *   t = func(a1, a2, ..., an)       (func is add/subtract/multiply/transpose/inverse)
 *   t = a                           (plain copy, e.g. X = Y or X = t3)
 *   PRINT a
 *   INPUT a RxC                     (a matrix passed in by the caller)
 *   OUTPUT a                        (the matrix handed back to the caller)
 */
typedef enum {
    TAC_LOAD_MATRIX,   /* create a matrix from a literal */
    TAC_GEN,           /* create identity(n) or zeros(r, c) */
    TAC_SCALE,         /* result = number * matrix */
    TAC_BINOP,         /* result = a + b, a - b or a * b */
    TAC_CALL,          /* result = add/subtract/multiply/transpose/inverse(...) */
    TAC_COPY,          /* result = another variable */
    TAC_PRINT,
    TAC_INPUT,
    TAC_OUTPUT
} TACOp;

#define TAC_NAME_MAX 64
#define TAC_MAX_CALL_ARGS 16   /* most arguments one call can hold */

/* One instruction. Like ASTNode, one struct serves every kind: `op` says
 * which kind, and each kind only uses the fields noted below. */
typedef struct {
    TACOp op;
    int line;                    /* source line it came from (for debugging) */

    char result[TAC_NAME_MAX];   /* every op except PRINT/OUTPUT writes a result;
                                    for INPUT it is the declared name */

    /* TAC_BINOP */
    char arg1[TAC_NAME_MAX];
    char arg2[TAC_NAME_MAX];
    char binop;                  /* '+', '-', '*' */

    /* TAC_SCALE: result = scalar * arg1 */
    double scalar;

    /* TAC_CALL */
    char func_name[16];          /* "add", "subtract", "multiply", "transpose", "inverse"
                                    (TAC_GEN: "identity", "zeros") */
    char call_args[TAC_MAX_CALL_ARGS][TAC_NAME_MAX];
    int call_arg_count;

    /* TAC_COPY / TAC_PRINT / TAC_OUTPUT */
    char src[TAC_NAME_MAX];
    int src_is_temp;             /* TAC_COPY: src is a temporary nothing else reads */

    /* TAC_GEN uses func_name ("identity" or "zeros") and rows / cols below */

    /* TAC_LOAD_MATRIX; also the size of a TAC_GEN / TAC_INPUT / TAC_OUTPUT matrix */
    int rows;
    int cols;
    double **matrix_values;      /* owned deep copy of the literal's numbers */
} TACInstr;

/* The whole program: a plain array of instructions, run from first to last. */
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
