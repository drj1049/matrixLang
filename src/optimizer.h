#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "ast.h"
#include "parser.h"

#define MAXN 64

long long mcm(int p[], int n, int split[MAXN][MAXN]);
void print_paren(int split[MAXN][MAXN], int i, int j);

/* takes an expression, returns it with multiply chains reordered */
ASTNode *optimize_expr(ASTNode *node);

/* runs the optimizer on every statement in the program */
void optimize_program(Program *prog);

#endif