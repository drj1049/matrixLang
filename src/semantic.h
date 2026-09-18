#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "parser.h"

/* Walks every statement in the program, checking that matrix dimensions
 * are compatible everywhere they're combined:
 *   - infix '+'/'-' and add/subtract: operand dims must match exactly
 *   - infix '*' and multiply: chain must be conformable (A.cols == B.rows)
 *   - transpose: always valid, swaps dims
 *   - inverse: operand must be square
 *   - every identifier used must have been assigned earlier
 * As a side effect, fills in result_rows/result_cols on every expression
 * node in the AST, so later stages (optimizer, codegen) don't need to
 * recompute dimensions themselves.
 * On any violation, prints an error with the offending line number and
 * exits(1), same convention as the lexer and parser use.
 */
void semantic_check(Program *prog);

#endif /* SEMANTIC_H */