#include <stdio.h>
#include "../src/ast.h"
#include "../src/optimizer.h"

static ASTNode *mat(const char *name, int r, int c) {
    ASTNode *n = ast_new_identifier(name, 1);
    n->result_rows = r;
    n->result_cols = c;
    return n;
}

int main() {
    ASTNode *A = mat("A", 10, 30);
    ASTNode *B = mat("B", 30, 5);
    ASTNode *C = mat("C", 5, 60);

    ASTNode *bc = ast_new_binop('*', B, C, 1);
    bc->result_rows = 30;
    bc->result_cols = 60;
    ASTNode *tree = ast_new_binop('*', A, bc, 1);
    tree->result_rows = 10;
    tree->result_cols = 60;

    printf("before:\n");
    ast_print(tree, 0);

    tree = optimize_expr(tree);

    printf("after:\n");
    ast_print(tree, 0);

    ast_free(tree);
    return 0;
}