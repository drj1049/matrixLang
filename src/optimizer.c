#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "optimizer.h"

long long mcm(int p[], int n, int split[MAXN][MAXN]) {
    long long m[MAXN][MAXN];
    for (int i = 1; i <= n; i++) m[i][i] = 0;

    for (int len = 2; len <= n; len++) {
        for (int i = 1; i + len - 1 <= n; i++) {
            int j = i + len - 1;
            m[i][j] = LLONG_MAX;
            for (int k = i; k < j; k++) {
                long long cost = m[i][k] + m[k+1][j]
                               + (long long)p[i-1] * p[k] * p[j];
                if (cost < m[i][j]) {
                    m[i][j] = cost;
                    split[i][j] = k;
                }
            }
        }
    }
    return m[1][n];
}

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
}

/* build a new tree using the split table from mcm */
static ASTNode *build(ASTNode **ops, int split[MAXN][MAXN], int i, int j, int line) {
    if (i == j) return ops[i - 1];
    int k = split[i][j];
    ASTNode *l = build(ops, split, i, k, line);
    ASTNode *r = build(ops, split, k + 1, j, line);
    ASTNode *b = ast_new_binop('*', l, r, line);
    b->result_rows = l->result_rows;
    b->result_cols = r->result_cols;
    return b;
}

static FILE *report_out = NULL;

void optimizer_set_report(FILE *f) { report_out = f; }

static void app(char *buf, size_t cap, const char *s) {
    size_t len = strlen(buf);
    if (len + 1 >= cap) return;
    strncat(buf, s, cap - len - 1);
}

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
            cols = n->args[i]->result_cols;
        }
        return c;
    }
    return 0;
}

static void best_str(ASTNode **ops, int split[MAXN][MAXN], int i, int j, char *buf, size_t cap) {
    if (i == j) { app(buf, cap, label(ops[i - 1])); return; }
    app(buf, cap, "(");
    best_str(ops, split, i, split[i][j], buf, cap);
    app(buf, cap, " x ");
    best_str(ops, split, split[i][j] + 1, j, buf, cap);
    app(buf, cap, ")");
}

ASTNode *optimize_expr(ASTNode *node) {
    if (node == NULL) return NULL;

    int is_chain = (node->type == NODE_BINOP && node->op == '*')
                || (node->type == NODE_FUNC_CALL && node->func_kind == TOK_MULTIPLY);

    if (is_chain) {
        ASTNode *ops[MAXN];
        int n = 0;
        flatten(node, ops, &n);

        if (n >= 3) {
            for (int i = 0; i < n; i++)
                ops[i] = optimize_expr(ops[i]);

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
            free_shells(node);
            return root;
        }
    }

    if (node->type == NODE_BINOP) {
        node->left = optimize_expr(node->left);
        node->right = optimize_expr(node->right);
    }
    return node;
}

void optimize_program(Program *prog) {
    for (int i = 0; i < prog->count; i++) {
        ASTNode *s = prog->statements[i];
        if (s->type == NODE_ASSIGN)
            s->value = optimize_expr(s->value);
    }
}