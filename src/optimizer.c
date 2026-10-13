/*
 * optimizer.c : picks the cheapest order for chains of matrix multiplications.
 *
 * HOW IT WORKS, STEP BY STEP (see optimize_chain)
 *   1. FLATTEN: collect all the matrices of a chain into one list. A*B*C*D
 *      written in any bracketing gives the same list [A, B, C, D]. This is
 *      why the optimizer ignores how the user grouped things: it may regroup.
 *   2. SIZES: the list of sizes p[]; matrix i is p[i-1] x p[i].
 *   3. DP (mcm): for every sub-chain i..j, find the cheapest cut point k, and
 *      remember it in split[i][j]. Cost of cutting at k =
 *         cost(i..k) + cost(k+1..j) + p[i-1] * p[k] * p[j]
 *      where the last term is the cost of the final multiply joining the two.
 *   4. BUILD: make a new tree following the split table, then free the old
 *      multiply nodes (but keep the matrices inside them).
 *
 * Only chains of 3 or more matrices can be improved (with 2 there is only one
 * order), so shorter ones are left alone.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "optimizer.h"

/* Matrix chain multiplication by dynamic programming.
 *   m[i][j]     = fewest scalar multiplications to compute matrices i..j
 *   split[i][j] = the k where the best cut for i..j is made
 * A single matrix costs nothing (m[i][i] = 0). We build longer and longer
 * chains (len = 2, 3, ...) so every smaller answer is ready when needed.
 * Time is O(n^3), which is tiny for n <= 63. */
long long mcm(int p[], int n, int split[MAXN][MAXN]) {
    long long m[MAXN][MAXN];
    for (int i = 1; i <= n; i++) m[i][i] = 0;

    for (int len = 2; len <= n; len++) {
        for (int i = 1; i + len - 1 <= n; i++) {
            int j = i + len - 1;
            m[i][j] = LLONG_MAX;    /* start at "infinity" so any real cost beats it */
            for (int k = i; k < j; k++) {
                long long cost = m[i][k] + m[k+1][j]
                               + (long long)p[i-1] * p[k] * p[j];   /* long long so big sizes cannot overflow */
                if (cost < m[i][j]) {
                    m[i][j] = cost;
                    split[i][j] = k;
                }
            }
        }
    }
    return m[1][n];
}

/* Prints the order as text, e.g. ((M1 x M2) x M3). It follows the split table:
 * chain i..j is cut at split[i][j], and each half is printed the same way. */
void print_paren(int split[MAXN][MAXN], int i, int j) {
    if (i == j) { printf("M%d", i); return; }
    printf("(");
    print_paren(split, i, split[i][j]);
    printf(" x ");
    print_paren(split, split[i][j] + 1, j);
    printf(")");
}

/* collect every matrix in a chain like A*B*C*D into ops[] */
static void flatten(ASTNode *n, ASTNode **ops, int *count) {
    if (n->type == NODE_BINOP && n->op == '*') {
        flatten(n->left, ops, count);
        flatten(n->right, ops, count);
    } else if (n->type == NODE_FUNC_CALL && n->func_kind == TOK_MULTIPLY) {
        for (int i = 0; i < n->arg_count; i++)
            flatten(n->args[i], ops, count);
    } else {
        /* anything else (a variable, a sum, a scale, ...) is one operand of the chain */
        if (*count >= MAXN - 1) {
            fprintf(stderr, "Optimizer error: multiply chain too long\n");
            exit(1);
        }
        ops[(*count)++] = n;
    }
}

/* free the old multiply nodes but keep the matrices inside them */
static void free_shells(ASTNode *n) {
    if (n->type == NODE_BINOP && n->op == '*') {
        free_shells(n->left);
        free_shells(n->right);
        free(n);
    } else if (n->type == NODE_FUNC_CALL && n->func_kind == TOK_MULTIPLY) {
        for (int i = 0; i < n->arg_count; i++)
            free_shells(n->args[i]);
        free(n->args);
        free(n);
    }
    /* any other node is a chain operand, which the new tree now owns, so leave it */
}

/* build a new tree using the split table from mcm.
 * Chain i..j is cut at k = split[i][j]: build the left part i..k, the right
 * part k+1..j, and join them with one multiply node. A single matrix (i == j)
 * is just the operand itself. Operands are numbered from 1, ops[] from 0. */
static ASTNode *build(ASTNode **ops, int split[MAXN][MAXN], int i, int j, int line) {
    if (i == j) return ops[i - 1];
    int k = split[i][j];
    ASTNode *l = build(ops, split, i, k, line);
    ASTNode *r = build(ops, split, k + 1, j, line);
    ASTNode *b = ast_new_binop('*', l, r, line);
    /* the semantic check already ran, so keep the size information correct on new nodes */
    b->result_rows = l->result_rows;
    b->result_cols = r->result_cols;
    return b;
}

/* ---- Optional report ----
 * The web page shows "as written vs best order" for each chain. When a report
 * file is set, optimize_chain writes one line per chain into it. */
static FILE *report_out = NULL;

void optimizer_set_report(FILE *f) { report_out = f; }

/* Safe string append: adds s to buf without ever going past `cap` bytes. */
static void app(char *buf, size_t cap, const char *s) {
    size_t len = strlen(buf);
    if (len + 1 >= cap) return;
    strncat(buf, s, cap - len - 1);
}

/* Short name of an operand for the report: the variable name, or "expr" for
 * anything more complicated. */
static const char *label(ASTNode *n) {
    return n->type == NODE_IDENTIFIER ? n->name : "expr";
}

/* the order a chain is written in, e.g. ((A x B) x C); multiply(a, b, c) folds left to right */
static void written_str(ASTNode *n, char *buf, size_t cap) {
    if (n->type == NODE_BINOP && n->op == '*') {
        app(buf, cap, "(");
        written_str(n->left, buf, cap);
        app(buf, cap, " x ");
        written_str(n->right, buf, cap);
        app(buf, cap, ")");
    } else if (n->type == NODE_FUNC_CALL && n->func_kind == TOK_MULTIPLY && n->arg_count >= 2) {
        /* multiply(a, b, c) means ((a x b) x c): open all brackets first, then close one per argument */
        for (int i = 1; i < n->arg_count; i++) app(buf, cap, "(");
        written_str(n->args[0], buf, cap);
        for (int i = 1; i < n->arg_count; i++) {
            app(buf, cap, " x ");
            written_str(n->args[i], buf, cap);
            app(buf, cap, ")");
        }
    } else {
        app(buf, cap, label(n));
    }
}

/* scalar multiplications needed to evaluate the chain in the order it is written */
static long long written_cost(ASTNode *n) {
    if (n->type == NODE_BINOP && n->op == '*') {
        return written_cost(n->left) + written_cost(n->right)
             + (long long)n->left->result_rows * n->left->result_cols * n->right->result_cols;
    }
    if (n->type == NODE_FUNC_CALL && n->func_kind == TOK_MULTIPLY && n->arg_count >= 2) {
        long long c = written_cost(n->args[0]);
        int rows = n->args[0]->result_rows, cols = n->args[0]->result_cols;
        for (int i = 1; i < n->arg_count; i++) {
            c += written_cost(n->args[i]) + (long long)rows * cols * n->args[i]->result_cols;
            cols = n->args[i]->result_cols;   /* the running product keeps its rows, takes the new columns */
        }
        return c;
    }
    return 0;   /* a single operand costs nothing to "multiply" */
}

/* the best order as text, built from the split table (like print_paren but into a string) */
static void best_str(ASTNode **ops, int split[MAXN][MAXN], int i, int j, char *buf, size_t cap) {
    if (i == j) { app(buf, cap, label(ops[i - 1])); return; }
    app(buf, cap, "(");
    best_str(ops, split, i, split[i][j], buf, cap);
    app(buf, cap, " x ");
    best_str(ops, split, split[i][j] + 1, j, buf, cap);
    app(buf, cap, ")");
}

ASTNode *optimize_expr(ASTNode *node);

/* Reorders one multiply chain. Returns the new tree, or NULL when the chain
 * has fewer than three matrices (nothing to reorder). This is a function of
 * its own because its tables are large, and optimize_expr recurses once per
 * operator: a long sum must not pay for those tables at every level. */
static ASTNode *optimize_chain(ASTNode *node) {
    ASTNode *ops[MAXN];
    int n = 0;
    flatten(node, ops, &n);
    if (n < 3) return NULL;

    /* each operand may contain its own chains inside (for example a sum of
     * products), so optimize those first */
    for (int i = 0; i < n; i++)
        ops[i] = optimize_expr(ops[i]);

    /* sizes for the DP: matrix i is p[i-1] x p[i] (consecutive matrices share
     * a dimension, which is exactly what makes them multipliable) */
    int p[MAXN];
    p[0] = ops[0]->result_rows;
    for (int i = 0; i < n; i++)
        p[i + 1] = ops[i]->result_cols;

    int split[MAXN][MAXN];
    long long best = mcm(p, n, split);

    if (report_out) {
        char wtxt[2048] = "", btxt[2048] = "";
        written_str(node, wtxt, sizeof wtxt);
        best_str(ops, split, 1, n, btxt, sizeof btxt);
        fprintf(report_out, "CHAIN\t%d\t", node->line);
        for (int i = 0; i < n; i++)
            fprintf(report_out, "%s%s:%dx%d", i ? "," : "", label(ops[i]),
                    ops[i]->result_rows, ops[i]->result_cols);
        fprintf(report_out, "\t%s\t%lld\t%s\t%lld\n", wtxt, written_cost(node), btxt, best);
    }

    ASTNode *root = build(ops, split, 1, n, node->line);
    free_shells(node);   /* after build: the operands are now inside the new tree */
    return root;
}

/* Walks an expression tree and reorders every multiply chain in it.
 * Returns the (possibly new) root, so callers must store the result back. */
ASTNode *optimize_expr(ASTNode *node) {
    if (node == NULL) return NULL;

    int is_chain = (node->type == NODE_BINOP && node->op == '*')
                || (node->type == NODE_FUNC_CALL && node->func_kind == TOK_MULTIPLY);

    if (is_chain) {
        ASTNode *root = optimize_chain(node);
        if (root) return root;   /* NULL means "too short to change": fall through */
    }

    /* not a chain: look for chains inside the children */
    if (node->type == NODE_BINOP) {
        node->left = optimize_expr(node->left);
        node->right = optimize_expr(node->right);
    } else if (node->type == NODE_SCALE) {
        node->left = optimize_expr(node->left);
    }
    return node;
}

/* Runs the optimizer over the right-hand side of every assignment. */
void optimize_program(Program *prog) {
    for (int i = 0; i < prog->count; i++) {
        ASTNode *s = prog->statements[i];
        if (s->type == NODE_ASSIGN)
            s->value = optimize_expr(s->value);
    }
}
