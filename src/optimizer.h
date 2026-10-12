#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include <stdio.h>
#include "ast.h"
#include "parser.h"

#define MAXN 64

long long mcm(int p[], int n, int split[MAXN][MAXN]);
void print_paren(int split[MAXN][MAXN], int i, int j);

/* takes an expression, returns it with multiply chains reordered */
ASTNode *optimize_expr(ASTNode *node);

/* when set, optimize_expr writes one tab-separated CHAIN line to f for every
 * multiply chain it rebuilds: line, operands, order as written and its cost,
 * best order and its cost (costs count scalar multiplications) */
void optimizer_set_report(FILE *f);

/* runs the optimizer on every statement in the program */
void optimize_program(Program *prog);

#endif