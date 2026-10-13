/*
 * semantic.c : checks the meaning of the program (types and sizes).
 *
 * In MatrixLang the only "type" is a matrix, and what matters about it is its
 * SIZE (rows x cols). So semantic analysis is a size checker. It has two
 * main parts:
 *
 *   1. A SYMBOL TABLE: a list of every variable seen so far, with its size.
 *   2. check_expr(): walks an expression tree from the bottom up and works
 *      out the size of every node, reporting an error when sizes do not fit
 *      (for example adding a 2x3 to a 3x3).
 *
 * The sizes it finds are stored back into the AST (result_rows/result_cols),
 * so the optimizer and code generator can just read them.
 *
 * ERROR RECOVERY works like in the parser: an error is saved, the checker
 * jumps (longjmp) to the next statement and keeps going. If an assignment
 * fails, its variable is marked "bad", and later statements that use it are
 * skipped silently. This way one real mistake does not create a flood of
 * follow-up errors.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <setjmp.h>
#include "semantic.h"
#include "diag.h"

/* ---- Symbol table ----
 * Tracks, for every variable assigned so far, what dimensions it holds.
 * A simple growable array with linear search is plenty for a language
 * with a handful of variables per program.
 */
typedef struct {
    char name[AST_NAME_MAX];
    int rows;
    int cols;
    int is_input;   /* declared with 'input': owned by the caller, read-only */
    int is_bad;     /* its definition had an error: uses of it are skipped quietly */
} Symbol;

typedef struct {
    Symbol *items;   /* array of symbols, grown with realloc */
    int count;
} SymbolTable;

/* Makes an empty table (calloc zeroes it: no items, count 0). */
static SymbolTable *symtab_new(void) {
    SymbolTable *st = calloc(1, sizeof(SymbolTable));
    return st;
}

/* Frees the table and its array. */
static void symtab_free(SymbolTable *st) {
    free(st->items);
    free(st);
}

/* Returns the index of `name` in the table, or -1 if it is not there. */
static int symtab_find(SymbolTable *st, const char *name) {
    for (int i = 0; i < st->count; i++) {
        if (strcmp(st->items[i].name, name) == 0) return i;
    }
    return -1;
}

/* Records/updates a variable's dimensions (reassignment overwrites, matching
 * ordinary variable semantics -- a name can be reused for a new shape).
 */
static void symtab_set(SymbolTable *st, const char *name, int rows, int cols) {
    int idx = symtab_find(st, name);
    if (idx == -1) {
        /* new variable: add a zeroed slot at the end */
        st->count++;
        st->items = realloc(st->items, sizeof(Symbol) * st->count);
        idx = st->count - 1;
        memset(&st->items[idx], 0, sizeof(Symbol));
        strncpy(st->items[idx].name, name, AST_NAME_MAX - 1);
    }
    st->items[idx].rows = rows;
    st->items[idx].cols = cols;
    st->items[idx].is_bad = 0;   /* a good assignment clears an earlier "bad" mark */
}

/* Remembers that this name's definition failed to check, so later uses of it
 * do not pile "undefined variable" errors on top of the real one. */
static void symtab_mark_bad(SymbolTable *st, const char *name) {
    int idx = symtab_find(st, name);
    if (idx == -1) {
        symtab_set(st, name, 0, 0);
        idx = symtab_find(st, name);
    }
    if (st->items[idx].is_input) return;   /* an input always keeps its declared size */
    st->items[idx].is_bad = 1;
}

/* Is this variable marked bad (its definition had an error)? */
static int symtab_is_bad(SymbolTable *st, const char *name) {
    int idx = symtab_find(st, name);
    return idx != -1 && st->items[idx].is_bad;
}

/* Was this variable declared with 'input'? (Inputs cannot be assigned to.) */
static int symtab_is_input(SymbolTable *st, const char *name) {
    int idx = symtab_find(st, name);
    return idx != -1 && st->items[idx].is_input;
}

/* Returns 1 and fills rows/cols if found, else returns 0. */
static int symtab_get(SymbolTable *st, const char *name, int *rows, int *cols) {
    int idx = symtab_find(st, name);
    if (idx == -1) return 0;
    *rows = st->items[idx].rows;
    *cols = st->items[idx].cols;
    return 1;
}

/* ---- Error handling ----
 * An error is written down and the checker jumps back to the statement loop
 * (setjmp/longjmp), so the next statement is checked too and one run can
 * report several mistakes. semantic_check prints them all at the end.
 */
#define MAX_SEM_ERRORS 20

/* One saved error: where it points (taken from an AST node) and the text. */
typedef struct {
    int line;
    int col;
    int len;
    char msg[256];
} SemError;

static SemError sem_errors[MAX_SEM_ERRORS];
static int sem_error_count;
static int sem_too_many;     /* set when a 21st error shows up (it is not stored) */
static jmp_buf stmt_jmp;     /* where longjmp returns to: the statement loop */

/* write an error down without leaving the current statement */
static void sem_note(const ASTNode *at, const char *fmt, ...) {
    if (sem_error_count >= MAX_SEM_ERRORS) {
        sem_too_many = 1;
        return;
    }
    va_list args;
    va_start(args, fmt);
    sem_errors[sem_error_count].line = at->line;
    sem_errors[sem_error_count].col = at->col;
    sem_errors[sem_error_count].len = at->len;
    vsnprintf(sem_errors[sem_error_count].msg, sizeof sem_errors[sem_error_count].msg, fmt, args);
    va_end(args);
    sem_error_count++;
}

/* write an error down and give up on the current statement
 * (does not return: longjmp leaves it) */
static void sem_error(const ASTNode *at, const char *fmt, ...) {
    if (sem_error_count >= MAX_SEM_ERRORS) {
        sem_too_many = 1;
    } else {
        va_list args;
        va_start(args, fmt);
        sem_errors[sem_error_count].line = at->line;
        sem_errors[sem_error_count].col = at->col;
        sem_errors[sem_error_count].len = at->len;
        vsnprintf(sem_errors[sem_error_count].msg, sizeof sem_errors[sem_error_count].msg, fmt, args);
        va_end(args);
        sem_error_count++;
    }
    longjmp(stmt_jmp, 1);
}

/* give up on the current statement without a new message, because the
 * variable it uses already had an error reported */
static void sem_skip_statement(void) {
    longjmp(stmt_jmp, 1);
}

/* ---- Core: check an expression node, filling in its result dimensions.
 * Recurses into children first (bottom-up), since a node's own validity
 * and output shape depend on its children's shapes.
 */
static void check_expr(SymbolTable *st, ASTNode *node) {
    switch (node->type) {

        case NODE_MATRIX_LITERAL:
            /* Dimensions are already known directly from the literal itself. */
            node->result_rows = node->rows;
            node->result_cols = node->cols;
            break;

        case NODE_SCALE:
            /* scaling keeps the size of the matrix being scaled */
            check_expr(st, node->left);
            node->result_rows = node->left->result_rows;
            node->result_cols = node->left->result_cols;
            break;

        case NODE_MATRIX_GEN:
            /* identity(n) and zeros(r, c) carry their size in the node */
            node->result_rows = node->rows;
            node->result_cols = node->cols;
            break;

        case NODE_IDENTIFIER: {
            /* the size of a variable is whatever the symbol table says */
            int r, c;
            if (!symtab_get(st, node->name, &r, &c)) {
                sem_error(node, "undefined variable '%s'", node->name);
            }
            if (symtab_is_bad(st, node->name)) sem_skip_statement();
            node->result_rows = r;
            node->result_cols = c;
            break;
        }

        case NODE_BINOP: {
            /* check both sides first, so their sizes are filled in */
            check_expr(st, node->left);
            check_expr(st, node->right);
            int lr = node->left->result_rows,  lc = node->left->result_cols;
            int rr = node->right->result_rows, rc = node->right->result_cols;

            if (node->op == '+' || node->op == '-') {
                /* add/subtract: both sides must have exactly the same size */
                if (lr != rr || lc != rc) {
                    sem_error(node,
                        "dimension mismatch for infix '%c': %dx%d vs %dx%d",
                        node->op, lr, lc, rr, rc);
                }
                node->result_rows = lr;
                node->result_cols = lc;
            } else { /* '*' */
                /* multiply: (m x n) * (n x p) gives (m x p); the two n's must match */
                if (lc != rr) {
                    sem_error(node,
                        "cannot multiply %dx%d by %dx%d (inner dimensions must match)",
                        lr, lc, rr, rc);
                }
                node->result_rows = lr;
                node->result_cols = rc;
            }
            break;
        }

        case NODE_FUNC_CALL: {
            /* Every arg is an identifier per the grammar -- resolve each one. */
            for (int i = 0; i < node->arg_count; i++) {
                check_expr(st, node->args[i]);
            }

            switch (node->func_kind) {
                case TOK_ADD:
                case TOK_SUBTRACT: {
                    /* All operands must share identical dimensions (checked
                     * pairwise against the first, same idea as folding
                     * left to right through (a+b)+c).
                     */
                    int base_r = node->args[0]->result_rows;
                    int base_c = node->args[0]->result_cols;
                    for (int i = 1; i < node->arg_count; i++) {
                        int ar = node->args[i]->result_rows;
                        int ac = node->args[i]->result_cols;
                        if (ar != base_r || ac != base_c) {
                            sem_error(node->args[i],
                                "argument %d has dims %dx%d, expected %dx%d to match the others (add/subtract require identical dimensions)",
                                i + 1, ar, ac, base_r, base_c);
                        }
                    }
                    node->result_rows = base_r;
                    node->result_cols = base_c;
                    break;
                }

                case TOK_MULTIPLY: {
                    /* Chain check: fold left to right, each step needs
                     * cur.cols == next.rows. Each arg's own dims are already
                     * on args[i]->result_rows/cols for the optimizer to
                     * read later when it runs the matrix-chain DP.
                     */
                    int cur_rows = node->args[0]->result_rows;
                    int cur_cols = node->args[0]->result_cols;
                    for (int i = 1; i < node->arg_count; i++) {
                        int nr = node->args[i]->result_rows;
                        int nc = node->args[i]->result_cols;
                        if (cur_cols != nr) {
                            sem_error(node->args[i],
                                "cannot chain-multiply: %dx%d by %dx%d at argument %d (inner dimensions must match)",
                                cur_rows, cur_cols, nr, nc, i + 1);
                        }
                        cur_cols = nc; /* rows stay fixed at the chain's first matrix */
                    }
                    node->result_rows = cur_rows;
                    node->result_cols = cur_cols;
                    break;
                }

                case TOK_TRANSPOSE: {
                    /* swapping rows and columns is always possible */
                    ASTNode *a = node->args[0];
                    node->result_rows = a->result_cols;
                    node->result_cols = a->result_rows;
                    break;
                }

                case TOK_INVERSE: {
                    /* only a square matrix can have an inverse */
                    ASTNode *a = node->args[0];
                    if (a->result_rows != a->result_cols) {
                        sem_error(a,
                            "inverse requires a square matrix, got %dx%d",
                            a->result_rows, a->result_cols);
                    }
                    node->result_rows = a->result_rows;
                    node->result_cols = a->result_cols;
                    break;
                }

                default:
                    sem_error(node, "internal error: unknown function kind");
            }
            break;
        }

        default:
            sem_error(node, "internal error: unexpected node in expression position");
    }
}

/* t0, t1, ... are the names tac.c gives its temporaries, so a user variable
 * with one of those names would silently share storage with a temporary.
 */
static void check_not_temp_name(const char *name, const ASTNode *at) {
    if (name[0] != 't' || name[1] == '\0') return;   /* must start with 't' and have more after it */
    for (const char *p = name + 1; *p; p++) {
        if (!isdigit((unsigned char)*p)) return;     /* a non-digit means it is a normal name like "total" */
    }
    sem_error(at, "'%s' is reserved for compiler temporaries, pick another name", name);
}

/* ---- Check a single top-level statement ---- */
static void check_statement(SymbolTable *st, ASTNode *stmt) {
    if (stmt->type == NODE_ASSIGN) {
        check_not_temp_name(stmt->target, stmt);
        if (symtab_is_input(st, stmt->target)) {
            sem_error(stmt, "cannot assign to input '%s' (inputs belong to the caller)",
                      stmt->target);
        }
        /* check the right side first: "A = A + B" must see the OLD size of A */
        check_expr(st, stmt->value);
        symtab_set(st, stmt->target, stmt->value->result_rows, stmt->value->result_cols);
    } else if (stmt->type == NODE_PRINT) {
        int r, c;
        if (!symtab_get(st, stmt->print_target, &r, &c)) {
            sem_error(stmt, "cannot print undefined variable '%s'", stmt->print_target);
        }
        if (symtab_is_bad(st, stmt->print_target)) sem_skip_statement();
    } else if (stmt->type == NODE_INPUT) {
        check_not_temp_name(stmt->name, stmt);
        if (symtab_find(st, stmt->name) != -1) {
            sem_error(stmt, "input '%s' is already defined", stmt->name);
        }
        symtab_set(st, stmt->name, stmt->rows, stmt->cols);
        st->items[symtab_find(st, stmt->name)].is_input = 1;
    } else if (stmt->type == NODE_OUTPUT) {
        /* also copies the size of the output matrix into the statement node */
        if (!symtab_get(st, stmt->name, &stmt->result_rows, &stmt->result_cols)) {
            sem_error(stmt, "cannot output undefined variable '%s'", stmt->name);
        }
        if (symtab_is_bad(st, stmt->name)) sem_skip_statement();
    } else {
        sem_error(stmt, "internal error: unexpected node at statement level");
    }
}

/* The entry point (see semantic.h). Checks all statements in order, then
 * prints every saved error (if any) and exits with code 1. */
void semantic_check(Program *prog) {
    SymbolTable *st = symtab_new();
    const ASTNode *first_input = NULL;   /* remembered for the "inputs but no output" error */
    int has_output = 0;

    sem_error_count = 0;
    sem_too_many = 0;

    for (int i = 0; i < prog->count; i++) {
        ASTNode *stmt = prog->statements[i];

        /* setjmp returns 0 now, and 1 if sem_error/sem_skip_statement jumps here */
        if (setjmp(stmt_jmp) == 0) {
            /* output is the function's return, so it has to come last */
            if (has_output) {
                if (stmt->type == NODE_OUTPUT) {
                    sem_error(stmt, "only one output is supported");
                }
                sem_error(stmt, "no statements are allowed after output");
            }
            check_statement(st, stmt);
        } else if (stmt->type == NODE_ASSIGN) {
            /* this assignment failed, so its target has no usable value */
            symtab_mark_bad(st, stmt->target);
        }

        if (stmt->type == NODE_INPUT && !first_input) first_input = stmt;
        if (stmt->type == NODE_OUTPUT) has_output = 1;

        if (sem_too_many) break;
    }

    /* a program with inputs is a function, and a function must return something */
    if (first_input && !has_output && !sem_too_many) {
        sem_note(first_input, "program declares inputs but has no output statement");
    }

    if (sem_error_count > 0) {
        /* list them in source order (the "no output" error is found last).
         * Insertion sort by line number; it is stable, so errors on the same
         * line keep the order they were found in. */
        for (int i = 1; i < sem_error_count; i++) {
            SemError e = sem_errors[i];
            int j = i - 1;
            while (j >= 0 && sem_errors[j].line > e.line) {
                sem_errors[j + 1] = sem_errors[j];
                j--;
            }
            sem_errors[j + 1] = e;
        }
        for (int i = 0; i < sem_error_count; i++) {
            diag_print(stderr, "Semantic", sem_errors[i].line, sem_errors[i].col,
                       sem_errors[i].len, sem_errors[i].msg);
        }
        if (sem_too_many) {
            fprintf(stderr, "Too many errors, stopping after %d.\n", MAX_SEM_ERRORS);
        } else if (sem_error_count > 1) {
            fprintf(stderr, "%d errors found.\n", sem_error_count);
        }
        exit(1);
    }
    symtab_free(st);
}
