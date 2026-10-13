/*
 * optimizer.h : the matrix chain multiplication optimizer (works on the AST).
 *
 * THE PROBLEM
 * Matrix multiplication is associative, so (A*B)*C and A*(B*C) give the same
 * answer, but they can cost very different amounts of work. Multiplying a
 * (p x q) matrix by a (q x r) matrix takes p*q*r scalar multiplications. With
 * A = 10x100, B = 100x5, C = 5x50:
 *     (A*B)*C  ->  10*100*5 + 10*5*50   =  7500
 *     A*(B*C)  ->  100*5*50 + 10*100*50 = 75000
 * This file finds the cheapest order with dynamic programming (the classic
 * "matrix chain multiplication" algorithm) and rewrites the tree to use it.
 */
#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include <stdio.h>
#include "ast.h"
#include "parser.h"

/* Most matrices one chain can hold (the DP tables are MAXN x MAXN). The
 * indexes start at 1, so the real limit is MAXN - 1 = 63 matrices. */
#define MAXN 64

/* Matrix chain DP. p[] holds the sizes: matrix i is p[i-1] x p[i], for
 * i = 1..n. Returns the lowest number of scalar multiplications, and fills
 * split[i][j] with the position where the chain i..j is best cut in two. */
long long mcm(int p[], int n, int split[MAXN][MAXN]);

/* Prints the best order as text using the split table, e.g. ((M1 x M2) x M3) */
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
