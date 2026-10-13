/*
 * interp.h : the interpreter, which runs TAC directly inside the compiler.
 *
 * Besides compiling to C, MatrixLang can simply RUN a program: the
 * interpreter goes through the TAC list one instruction at a time and does
 * the real matrix arithmetic. This is what produces the numbers shown in the
 * "Output" tab, with no C compiler needed.
 */
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
