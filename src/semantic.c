#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "semantic.h"

/* ---- Symbol table ----
 * Tracks, for every variable assigned so far, what dimensions it holds.
 * A simple growable array with linear search is plenty for a language
 * with a handful of variables per program.
 */
typedef struct {
    char name[AST_NAME_MAX];
    int rows;
    int cols;
} Symbol;

typedef struct {
    Symbol *items;
    int count;
} SymbolTable;

static SymbolTable *symtab_new(void) {
    SymbolTable *st = calloc(1, sizeof(SymbolTable));
    return st;
}

static void symtab_free(SymbolTable *st) {
    free(st->items);
    free(st);
}

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
        st->count++;
        st->items = realloc(st->items, sizeof(Symbol) * st->count);
        idx = st->count - 1;
        strncpy(st->items[idx].name, name, AST_NAME_MAX - 1);
    }
    st->items[idx].rows = rows;
    st->items[idx].cols = cols;
}

/* Returns 1 and fills rows/cols if found, else returns 0. */
static int symtab_get(SymbolTable *st, const char *name, int *rows, int *cols) {
    int idx = symtab_find(st, name);
    if (idx == -1) return 0;
    *rows = st->items[idx].rows;
    *cols = st->items[idx].cols;
    return 1;
}

/* ---- Error helper ---- */
static void sem_error(int line, const char *fmt, ...) {
    fprintf(stderr, "Semantic error at line %d: ", line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    exit(1);
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

        case NODE_IDENTIFIER: {
            int r, c;
            if (!symtab_get(st, node->name, &r, &c)) {
                sem_error(node->line, "undefined variable '%s'", node->name);
            }
            node->result_rows = r;
            node->result_cols = c;
            break;
        }

        case NODE_BINOP: {
            check_expr(st, node->left);
            check_expr(st, node->right);
            int lr = node->left->result_rows,  lc = node->left->result_cols;
            int rr = node->right->result_rows, rc = node->right->result_cols;

            if (node->op == '+' || node->op == '-') {
                if (lr != rr || lc != rc) {
                    sem_error(node->line,
                        "dimension mismatch for infix '%c': %dx%d vs %dx%d",
                        node->op, lr, lc, rr, rc);
                }
                node->result_rows = lr;
                node->result_cols = lc;
            } else { /* '*' */
                if (lc != rr) {
                    sem_error(node->line,
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
                            sem_error(node->line,
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
                            sem_error(node->line,
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
                    ASTNode *a = node->args[0];
                    node->result_rows = a->result_cols;
                    node->result_cols = a->result_rows;
                    break;
                }

                case TOK_INVERSE: {
                    ASTNode *a = node->args[0];
                    if (a->result_rows != a->result_cols) {
                        sem_error(node->line,
                            "inverse requires a square matrix, got %dx%d",
                            a->result_rows, a->result_cols);
                    }
                    node->result_rows = a->result_rows;
                    node->result_cols = a->result_cols;
                    break;
                }

                default:
                    sem_error(node->line, "internal error: unknown function kind");
            }
            break;
        }

        default:
            sem_error(node->line, "internal error: unexpected node in expression position");
    }
}

/* ---- Check a single top-level statement ---- */
static void check_statement(SymbolTable *st, ASTNode *stmt) {
    if (stmt->type == NODE_ASSIGN) {
        check_expr(st, stmt->value);
        symtab_set(st, stmt->target, stmt->value->result_rows, stmt->value->result_cols);
    } else if (stmt->type == NODE_PRINT) {
        int r, c;
        if (!symtab_get(st, stmt->print_target, &r, &c)) {
            sem_error(stmt->line, "cannot print undefined variable '%s'", stmt->print_target);
        }
    } else {
        sem_error(stmt->line, "internal error: unexpected node at statement level");
    }
}

void semantic_check(Program *prog) {
    SymbolTable *st = symtab_new();
    for (int i = 0; i < prog->count; i++) {
        check_statement(st, prog->statements[i]);
    }
    symtab_free(st);
}