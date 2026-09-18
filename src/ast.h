#ifndef AST_H
#define AST_H

#include "lexer.h"  /* for TokenType, used to tag which function a call is */

/* ---- What kind of AST node this is ----
 * A statement is either an ASSIGN ("X = ...") or a PRINT ("print(X)").
 * The right-hand side of an ASSIGN is always an expression node:
 * IDENTIFIER, MATRIX_LITERAL, BINOP (infix add/sub/mul), or FUNC_CALL
 * (add/subtract/multiply/transpose/inverse).
 */
typedef enum {
    NODE_NUMBER,          /* a bare number, e.g. inside a matrix literal */
    NODE_MATRIX_LITERAL,  /* [[1,2],[3,4]] */
    NODE_IDENTIFIER,      /* A, B, C ... refers to a previously assigned var */
    NODE_BINOP,           /* A + B, A - B, A * B (infix, always 2 operands) */
    NODE_FUNC_CALL,       /* add(A,B,C), multiply(A,B,C,D), transpose(A), inverse(A) */
    NODE_ASSIGN,          /* X = <expr> */
    NODE_PRINT             /* print(X) */
} NodeType;

#define AST_NAME_MAX 64

typedef struct ASTNode {
    NodeType type;
    int line;

    /* NODE_IDENTIFIER */
    char name[AST_NAME_MAX];

    /* NODE_MATRIX_LITERAL: a rows x cols grid of numbers */
    double **values;
    int rows;
    int cols;

    /* NODE_BINOP: op is '+', '-', or '*' */
    char op;
    struct ASTNode *left;
    struct ASTNode *right;

    /* NODE_FUNC_CALL: func_kind is TOK_ADD / TOK_SUBTRACT / TOK_MULTIPLY /
     * TOK_TRANSPOSE / TOK_INVERSE. args is a variadic list of operand
     * nodes (each one is a NODE_IDENTIFIER per the grammar: argList -> ID (',' ID)*).
     * add/subtract/multiply: 2 or more args. transpose/inverse: exactly 1.
     */
    TokenType func_kind;
    struct ASTNode **args;
    int arg_count;

    /* NODE_ASSIGN */
    char target[AST_NAME_MAX];
    struct ASTNode *value;

    /* NODE_PRINT */
    char print_target[AST_NAME_MAX];
} ASTNode;

/* ---- Constructors ---- one per node kind, each mallocs and fills in the node. */
ASTNode *ast_new_identifier(const char *name, int line);
ASTNode *ast_new_matrix_literal(double **values, int rows, int cols, int line);
ASTNode *ast_new_binop(char op, ASTNode *left, ASTNode *right, int line);
ASTNode *ast_new_func_call(TokenType func_kind, ASTNode **args, int arg_count, int line);
ASTNode *ast_new_assign(const char *target, ASTNode *value, int line);
ASTNode *ast_new_print(const char *target, int line);

/* Recursively frees a node and everything it owns. */
void ast_free(ASTNode *node);

/* Debug pretty-printer: prints the tree structure indented, so you can
 * visually confirm the parser built what you expect. */
void ast_print(const ASTNode *node, int indent);

#endif /* AST_H */