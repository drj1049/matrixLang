#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "tac.h"

/* Writes a complete C program for the given TAC to out. */
void codegen_c(const TACProgram *tac, FILE *out);

#endif