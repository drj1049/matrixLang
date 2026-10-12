#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "tac.h"

/* 1 if the program has an input or output statement. Such a program
 * compiles to a callable C function instead of a main(). */
int codegen_is_function(const TACProgram *tac);

/* Writes the C code for the given TAC to out: a complete program with a
 * main(), or a function called func_name when codegen_is_function(). */
void codegen_c(const TACProgram *tac, const char *func_name, FILE *out);

/* Writes a header declaring that function. Only for codegen_is_function()
 * programs; needs the output statement that semantic_check guarantees
 * whenever there are inputs. */
void codegen_header(const TACProgram *tac, const char *func_name, FILE *out);

#endif
