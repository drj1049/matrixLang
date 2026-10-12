#include <stdio.h>
#include <stdlib.h>
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
            mcm(p, n, split);

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