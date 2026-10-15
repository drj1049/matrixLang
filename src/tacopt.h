/*
 * tacopt.h : optimizations on the three-address code.
 *
 * This is the second optimizer. The first one (optimizer.c) works on the tree
 * and only reorders multiplications. This one works on the flat TAC list and
 * removes repeated work and does simple algebra (X * identity = X, ...).
 * The technique is called LOCAL VALUE NUMBERING, explained in tacopt.c.
 * A third step, dead code elimination, then removes instructions whose
 * result nothing reads (see tac_eliminate_dead at the end of this file).
 */
#ifndef TACOPT_H
#define TACOPT_H

#include <stdio.h>
#include "tac.h"

/* ---- Optimizations on three-address code ----
 * One pass over the (straight-line) TAC using local value numbering: every
 * value the program computes gets a number, and two instructions that
 * compute the same value get the same number. That gives two things:
 *
 *   - Common subexpression elimination: if A * B was already computed and
 *     some variable still holds it, the second A * B becomes a plain copy.
 *   - Algebraic simplification, using what is known about each value:
 *       X * identity(n), identity(n) * X   ->  X
 *       X + zeros, zeros + X, X - zeros    ->  X
 *       X * zeros, zeros * X, 0 * X        ->  zeros of the result size
 *       1 * X                              ->  X
 *       transpose(transpose(X))            ->  X
 *       inverse(inverse(X))                ->  X
 *       transpose(identity), inverse(identity)  ->  the identity
 *       transpose(zeros)                   ->  zeros of the swapped size
 *
 * Instructions are rewritten in place (to a TAC_COPY or a TAC_GEN) and never
 * removed or moved, so the program computes the same values in the same
 * order. Only meant to run on the output of tac_generate().
 *
 * If notes is not NULL, one line per change is written to it:
 *     REUSE <tab> line <tab> text
 *     REWRITE <tab> line <tab> text
 * Returns how many instructions were changed.
 */
int tac_optimize(TACProgram *tac, FILE *notes);

/* ---- Dead code elimination ----
 * Removes every instruction whose result is never read, working backwards
 * through the (straight-line) program. Run it AFTER tac_optimize(), which
 * tends to leave work behind that nothing uses any more (for example the
 * identity(2) in A * identity(2), once that product became a plain copy).
 *
 * Never removed: PRINT, OUTPUT, INPUT (a function's inputs are part of its
 * signature), and inverse(...), because inverting a singular matrix is a
 * runtime error that dropping the call would hide. Surviving instructions
 * keep their order.
 *
 * If notes is not NULL, one line per removed assignment is written to it:
 *     REMOVE <tab> line <tab> text
 * Returns how many instructions were removed.
 */
int tac_eliminate_dead(TACProgram *tac, FILE *notes);

#endif /* TACOPT_H */
