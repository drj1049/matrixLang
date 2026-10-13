/*
 * diag.h : pretty error messages (diagnostics).
 *
 * A plain message like "expected an identifier" is hard to use when the
 * program is long. So the compiler prints the offending source line and puts
 * a ^ under the problem, like this:
 *
 *   Parse error at line 3, column 9: expected an identifier ...
 *      3 | R = A +
 *        |         ^
 *
 * HOW IT IS USED
 * The driver gives the program text to diag_set_source() once. After that the
 * parser and the semantic checker call diag_print() for each error they
 * report. diag.c looks the line up in the saved text and draws the caret.
 */
#ifndef DIAG_H
#define DIAG_H

#include <stdio.h>

/* Keeps a private copy of the program text (replacing any earlier one).
 * Passing NULL just forgets the old text. */
void diag_set_source(const char *src);

/* Prints one error. `kind` is "Parse" or "Semantic". col is counted from 1
 * and len is how many characters to underline; col 0 means "position
 * unknown", and then only the message line is printed. The source line is
 * left out when the line does not exist (for example the end of the file). */
void diag_print(FILE *out, const char *kind, int line, int col, int len, const char *msg);

#endif /* DIAG_H */
