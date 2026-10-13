/*
 * ast.h : the AST (Abstract Syntax Tree), the data structure the parser builds.
 *
 * WHAT IS AN AST?
 * After the lexer cuts the program into tokens, the parser checks that they
 * follow the grammar and builds a TREE that shows the structure of the
 * program. Every later stage (semantic check, optimizer, TAC generator) works
 * on this tree instead of on text. For example
 *
 *     C = (A + B) * 2
 *
 * becomes
 *
 *     Assign(C)
 *       Scale(2)
 *         BinOp(+)
 *           Identifier(A)
 *           Identifier(B)
 *
 * The brackets are gone, because the shape of the tree already says that
 * A + B is added first.
 *
 * ONE STRUCT FOR ALL NODES
 * To keep the code simple, every kind of node uses the same struct, ASTNode.
 * The field `type` says which kind it is, and each kind only uses the fields
 * that belong to it (the comments inside the struct say which). The unused
 * fields stay zero, because the constructors create nodes with calloc.
 */
#ifndef AST_H
#define AST_H

#include "lexer.h"  /* for TokenType, used to tag which function a call is */

/* ---- What kind of AST node this is ----
 * A statement is either an ASSIGN ("X = ..."), a PRINT ("print(X)"),
 * an INPUT ("input A 2x3") or an OUTPUT ("output C").
 * The right-hand side of an ASSIGN is always an expression node:
 * IDENTIFIER, MATRIX_LITERAL, BINOP (infix add/sub/mul), or FUNC_CALL
 * (add/subtract/multiply/transpose/inverse), or MATRIX_GEN (identity/zeros),
 * or SCALE (a number times a matrix).
 */
typedef enum {
    NODE_NUMBER,          /* a bare number in an expression; only the parser sees these:
                             it folds each one into a NODE_SCALE, or rejects it */
    NODE_MATRIX_LITERAL,  /* [[1,2],[3,4]] */
    NODE_IDENTIFIER,      /* A, B, C ... refers to a previously assigned var */
    NODE_BINOP,           /* A + B, A - B, A * B (infix, always 2 operands) */
    NODE_FUNC_CALL,       /* add(A,B,C), multiply(A,B,C,D), transpose(A), inverse(A) */
    NODE_MATRIX_GEN,      /* identity(3), zeros(2,3): a matrix built from its size alone */
    NODE_SCALE,           /* 2 * A, A * 2, -A: every element of a matrix times a number */
    NODE_ASSIGN,          /* X = <expr> */
    NODE_PRINT,           /* print(X) */
    NODE_INPUT,           /* input A 2x3: a matrix the caller passes in */
    NODE_OUTPUT           /* output C: the matrix the generated function returns */
} NodeType;

/* Longest variable name we keep, including the ending '\0'. */
#define AST_NAME_MAX 64

typedef struct ASTNode {
    NodeType type;
    int line;   /* source line this node came from, for error messages */
    int col;    /* where this node starts on its line (counting from 1), 0 if unknown */
    int len;    /* how many characters to underline when reporting an error on it */

    /* NODE_IDENTIFIER, NODE_INPUT, NODE_OUTPUT */
    char name[AST_NAME_MAX];

    /* NODE_MATRIX_LITERAL: a rows x cols grid of numbers.
     * NODE_INPUT: the declared size (values stays NULL).
     * NODE_MATRIX_GEN: the size of the generated matrix (func_kind says
     * which one: TOK_IDENTITY or TOK_ZEROS; values stays NULL). */
    double **values;
    int rows;
    int cols;

    /* NODE_NUMBER: the value. NODE_SCALE: the number every element is
     * multiplied by; the matrix expression it scales is in left. */
    double number;

    /* NODE_BINOP: op is '+', '-', or '*'. left and right are the two
     * operands. NODE_SCALE also uses left, for the matrix being scaled. */
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

    /* Filled in by semantic analysis (0 until then): the dimensions this
     * expression evaluates to. Valid on IDENTIFIER, MATRIX_LITERAL, BINOP,
     * and FUNC_CALL nodes. The optimizer reads these later instead of
     * recomputing dimensions itself. Also set on OUTPUT, so the code
     * generator can document the size of the returned matrix.
     */
    int result_rows;
    int result_cols;

    /* NODE_ASSIGN: the variable being assigned and the expression tree on
     * the right-hand side */
    char target[AST_NAME_MAX];
    struct ASTNode *value;

    /* NODE_PRINT: the variable to print */
    char print_target[AST_NAME_MAX];
} ASTNode;

/* ---- Constructors ---- one per node kind, each mallocs and fills in the node.
 * `line` is the source line the node came from. The node takes OWNERSHIP of
 * the pointers it is given (children, argument lists, matrix numbers), so
 * ast_free() later frees all of them together. */
ASTNode *ast_new_identifier(const char *name, int line);
ASTNode *ast_new_matrix_literal(double **values, int rows, int cols, int line);
ASTNode *ast_new_number(double value, int line);
ASTNode *ast_new_scale(double factor, ASTNode *matrix, int line);
ASTNode *ast_new_binop(char op, ASTNode *left, ASTNode *right, int line);
ASTNode *ast_new_func_call(TokenType func_kind, ASTNode **args, int arg_count, int line);
ASTNode *ast_new_matrix_gen(TokenType kind, int rows, int cols, int line);
ASTNode *ast_new_assign(const char *target, ASTNode *value, int line);
ASTNode *ast_new_print(const char *target, int line);
ASTNode *ast_new_input(const char *name, int rows, int cols, int line);
ASTNode *ast_new_output(const char *name, int line);

/* Records where a node sits in the source, so errors about it can point at
 * the right place. Returns the node, so it can wrap a constructor call. */
ASTNode *ast_at(ASTNode *node, int col, int len);

/* Recursively frees a node and everything it owns. */
void ast_free(ASTNode *node);

/* Debug pretty-printer: prints the tree structure indented, so you can
 * visually confirm the parser built what you expect. */
void ast_print(const ASTNode *node, int indent);

#endif /* AST_H */
