#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

static ASTNode *ast_alloc(NodeType type, int line) {
    ASTNode *n = calloc(1, sizeof(ASTNode)); /* calloc zeroes everything -> safe defaults */
    n->type = type;
    n->line = line;
    return n;
}

ASTNode *ast_new_identifier(const char *name, int line) {
    ASTNode *n = ast_alloc(NODE_IDENTIFIER, line);
    strncpy(n->name, name, AST_NAME_MAX - 1);
    return n;
}

ASTNode *ast_new_matrix_literal(double **values, int rows, int cols, int line) {
    ASTNode *n = ast_alloc(NODE_MATRIX_LITERAL, line);
    n->values = values;
    n->rows = rows;
    n->cols = cols;
    return n;
}

ASTNode *ast_new_binop(char op, ASTNode *left, ASTNode *right, int line) {
    ASTNode *n = ast_alloc(NODE_BINOP, line);
    n->op = op;
    n->left = left;
    n->right = right;
    return n;
}

ASTNode *ast_new_func_call(TokenType func_kind, ASTNode **args, int arg_count, int line) {
    ASTNode *n = ast_alloc(NODE_FUNC_CALL, line);
    n->func_kind = func_kind;
    n->args = args;
    n->arg_count = arg_count;
    return n;
}

ASTNode *ast_new_assign(const char *target, ASTNode *value, int line) {
    ASTNode *n = ast_alloc(NODE_ASSIGN, line);
    strncpy(n->target, target, AST_NAME_MAX - 1);
    n->value = value;
    return n;
}

ASTNode *ast_new_print(const char *target, int line) {
    ASTNode *n = ast_alloc(NODE_PRINT, line);
    strncpy(n->print_target, target, AST_NAME_MAX - 1);
    return n;
}

void ast_free(ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case NODE_MATRIX_LITERAL:
            for (int i = 0; i < node->rows; i++) free(node->values[i]);
            free(node->values);
            break;
        case NODE_BINOP:
            ast_free(node->left);
            ast_free(node->right);
            break;
        case NODE_FUNC_CALL:
            for (int i = 0; i < node->arg_count; i++) ast_free(node->args[i]);
            free(node->args);
            break;
        case NODE_ASSIGN:
            ast_free(node->value);
            break;
        default:
            break; /* NODE_NUMBER, NODE_IDENTIFIER, NODE_PRINT own no children */
    }
    free(node);
}

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

static const char *func_kind_name(TokenType t) {
    switch (t) {
        case TOK_ADD:       return "add";
        case TOK_SUBTRACT:  return "subtract";
        case TOK_MULTIPLY:  return "multiply";
        case TOK_TRANSPOSE: return "transpose";
        case TOK_INVERSE:   return "inverse";
        default:            return "?";
    }
}

void ast_print(const ASTNode *node, int indent) {
    if (!node) return;
    print_indent(indent);

    switch (node->type) {
        case NODE_IDENTIFIER:
            printf("Identifier(%s)\n", node->name);
            break;

        case NODE_MATRIX_LITERAL:
            printf("MatrixLiteral(%dx%d)\n", node->rows, node->cols);
            for (int r = 0; r < node->rows; r++) {
                print_indent(indent + 1);
                for (int c = 0; c < node->cols; c++) {
                    printf("%.4g ", node->values[r][c]);
                }
                printf("\n");
            }
            break;

        case NODE_BINOP:
            printf("BinOp(%c)\n", node->op);
            ast_print(node->left, indent + 1);
            ast_print(node->right, indent + 1);
            break;

        case NODE_FUNC_CALL:
            printf("FuncCall(%s, %d args)\n", func_kind_name(node->func_kind), node->arg_count);
            for (int i = 0; i < node->arg_count; i++) {
                ast_print(node->args[i], indent + 1);
            }
            break;

        case NODE_ASSIGN:
            printf("Assign(%s) [line %d]\n", node->target, node->line);
            ast_print(node->value, indent + 1);
            break;

        case NODE_PRINT:
            printf("Print(%s) [line %d]\n", node->print_target, node->line);
            break;

        default:
            printf("<unknown node>\n");
    }
}