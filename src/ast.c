/*
 * ast.c : creating, freeing and printing AST nodes (the structure itself is
 * explained in ast.h).
 *
 * Three jobs live here:
 *   1. constructors   : ast_new_xxx() build one kind of node each
 *   2. ast_free()     : gives all the memory of a tree back
 *   3. ast_print()    : draws a tree as indented text, for debugging and for
 *                       the web page's "tree before and after" view
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

/* Allocates one blank node of the given kind. Every constructor starts here.
 * calloc fills the memory with zeros, so every field a kind does not use is
 * 0 or NULL, which is a safe default (and lets ast_free skip them). */
static ASTNode *ast_alloc(NodeType type, int line) {
    ASTNode *n = calloc(1, sizeof(ASTNode)); /* calloc zeroes everything -> safe defaults */
    n->type = type;
    n->line = line;
    return n;
}

/* Stores the column and underline length on a node. It returns the same node
 * so the parser can write ast_at(ast_new_xxx(...), col, len) in one line. */
ASTNode *ast_at(ASTNode *node, int col, int len) {
    node->col = col;
    node->len = len;
    return node;
}

/* ---- Constructors ----
 * Each one creates a node of one kind and fills in only the fields that kind
 * uses. Names are copied with strncpy and the node is zero filled, so the
 * name is always properly ended with '\0'. */

/* a variable used in an expression: A */
ASTNode *ast_new_identifier(const char *name, int line) {
    ASTNode *n = ast_alloc(NODE_IDENTIFIER, line);
    strncpy(n->name, name, AST_NAME_MAX - 1);
    return n;
}

/* [[1,2],[3,4]]: `values` is an array of `rows` rows, each with `cols`
 * numbers. The node now owns that memory. */
ASTNode *ast_new_matrix_literal(double **values, int rows, int cols, int line) {
    ASTNode *n = ast_alloc(NODE_MATRIX_LITERAL, line);
    n->values = values;
    n->rows = rows;
    n->cols = cols;
    return n;
}

/* identity(n) or zeros(r, c). `kind` is TOK_IDENTITY or TOK_ZEROS. Only the
 * size is stored, the numbers are made later when the program runs. */
ASTNode *ast_new_matrix_gen(TokenType kind, int rows, int cols, int line) {
    ASTNode *n = ast_alloc(NODE_MATRIX_GEN, line);
    n->func_kind = kind;
    n->rows = rows;
    n->cols = cols;
    return n;
}

/* a bare number such as 2. It only exists while parsing: the parser turns it
 * into a NODE_SCALE as soon as it meets the matrix it multiplies. */
ASTNode *ast_new_number(double value, int line) {
    ASTNode *n = ast_alloc(NODE_NUMBER, line);
    n->number = value;
    return n;
}

/* factor * matrix: every element of `matrix` times the number `factor` */
ASTNode *ast_new_scale(double factor, ASTNode *matrix, int line) {
    ASTNode *n = ast_alloc(NODE_SCALE, line);
    n->number = factor;
    n->left = matrix;
    return n;
}

/* left op right, where op is '+', '-' or '*' */
ASTNode *ast_new_binop(char op, ASTNode *left, ASTNode *right, int line) {
    ASTNode *n = ast_alloc(NODE_BINOP, line);
    n->op = op;
    n->left = left;
    n->right = right;
    return n;
}

/* add(...), subtract(...), multiply(...), transpose(...) or inverse(...).
 * `args` is an array of arg_count identifier nodes; the node owns it. */
ASTNode *ast_new_func_call(TokenType func_kind, ASTNode **args, int arg_count, int line) {
    ASTNode *n = ast_alloc(NODE_FUNC_CALL, line);
    n->func_kind = func_kind;
    n->args = args;
    n->arg_count = arg_count;
    return n;
}

/* target = value (a whole statement) */
ASTNode *ast_new_assign(const char *target, ASTNode *value, int line) {
    ASTNode *n = ast_alloc(NODE_ASSIGN, line);
    strncpy(n->target, target, AST_NAME_MAX - 1);
    n->value = value;
    return n;
}

/* print(target) (a whole statement) */
ASTNode *ast_new_print(const char *target, int line) {
    ASTNode *n = ast_alloc(NODE_PRINT, line);
    strncpy(n->print_target, target, AST_NAME_MAX - 1);
    return n;
}

/* input name rows x cols (a whole statement). The matrix has this size but
 * no numbers yet: the caller of the generated function supplies them. */
ASTNode *ast_new_input(const char *name, int rows, int cols, int line) {
    ASTNode *n = ast_alloc(NODE_INPUT, line);
    strncpy(n->name, name, AST_NAME_MAX - 1);
    n->rows = rows;
    n->cols = cols;
    return n;
}

/* output name (a whole statement): the matrix the generated function returns */
ASTNode *ast_new_output(const char *name, int line) {
    ASTNode *n = ast_alloc(NODE_OUTPUT, line);
    strncpy(n->name, name, AST_NAME_MAX - 1);
    return n;
}

/* Frees a node and everything below it. It works from the bottom up: first
 * the children (and the arrays the node owns), then the node itself.
 * Passing NULL is allowed and does nothing. */
void ast_free(ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case NODE_MATRIX_LITERAL:
            /* the grid of numbers: free every row, then the array of rows */
            for (int i = 0; i < node->rows; i++) free(node->values[i]);
            free(node->values);
            break;
        case NODE_SCALE:
        case NODE_BINOP:
            /* a scale only has `left`, so `right` is NULL and ast_free skips it */
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
            break; /* NODE_NUMBER, NODE_IDENTIFIER, NODE_MATRIX_GEN, NODE_PRINT, NODE_INPUT, NODE_OUTPUT own no children */
    }
    free(node);
}

/* ---- Printing a tree ----
 * Each level of the tree is printed two spaces further to the right. */
static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

/* the word for a function kind, used when printing calls and identity/zeros */
static const char *func_kind_name(TokenType t) {
    switch (t) {
        case TOK_ADD:       return "add";
        case TOK_SUBTRACT:  return "subtract";
        case TOK_MULTIPLY:  return "multiply";
        case TOK_TRANSPOSE: return "transpose";
        case TOK_INVERSE:   return "inverse";
        case TOK_IDENTITY:  return "identity";
        case TOK_ZEROS:     return "zeros";
        default:            return "?";
    }
}

/* Prints a tree, one node per line, children indented under their parent.
 * Once semantic analysis has run, expression nodes also show their result
 * size as "-> 2x3" (result_rows is 0 before that, so nothing is shown). */
void ast_print(const ASTNode *node, int indent) {
    if (!node) return;
    print_indent(indent);

    switch (node->type) {
        case NODE_IDENTIFIER:
            printf("Identifier(%s)", node->name);
            if (node->result_rows > 0) printf(" -> %dx%d", node->result_rows, node->result_cols);
            printf("\n");
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

        case NODE_NUMBER:
            printf("Number(%g)\n", node->number);
            break;

        case NODE_SCALE:
            printf("Scale(%g)", node->number);
            if (node->result_rows > 0) printf(" -> %dx%d", node->result_rows, node->result_cols);
            printf("\n");
            ast_print(node->left, indent + 1);
            break;

        case NODE_MATRIX_GEN:
            printf("MatrixGen(%s, %dx%d)\n", func_kind_name(node->func_kind), node->rows, node->cols);
            break;

        case NODE_BINOP:
            printf("BinOp(%c)", node->op);
            if (node->result_rows > 0) printf(" -> %dx%d", node->result_rows, node->result_cols);
            printf("\n");
            ast_print(node->left, indent + 1);
            ast_print(node->right, indent + 1);
            break;

        case NODE_FUNC_CALL:
            printf("FuncCall(%s, %d args)", func_kind_name(node->func_kind), node->arg_count);
            if (node->result_rows > 0) printf(" -> %dx%d", node->result_rows, node->result_cols);
            printf("\n");
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

        case NODE_INPUT:
            printf("Input(%s, %dx%d) [line %d]\n", node->name, node->rows, node->cols, node->line);
            break;

        case NODE_OUTPUT:
            printf("Output(%s) [line %d]\n", node->name, node->line);
            break;

        default:
            printf("<unknown node>\n");
    }
}
