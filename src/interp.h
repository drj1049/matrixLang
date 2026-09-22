#ifndef INTERP_H
#define INTERP_H

#include "tac.h"

/* Executes a TAC program: does the actual matrix arithmetic (add, subtract,
 * multiply, transpose, inverse) and prints real numbers for every PRINT
 * instruction. This is the "run it and see the answer" stage, on top of
 * everything that just checks correctness.
 */
void interp_run(const TACProgram *tac);

#endif /* INTERP_H */