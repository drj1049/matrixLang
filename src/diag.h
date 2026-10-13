#ifndef DIAG_H
#define DIAG_H

#include <stdio.h>

/* Error messages that show the offending source line with a ^ under the
 * problem, like this:
 *
 *   Parse error at line 3, column 9: expected an identifier ...
 *      3 | R = A +
 *        |         ^
 *
 * The parser stores the program text once with diag_set_source; the parser
 * and the semantic checker then call diag_print for each error they report.
 */

/* Keeps a copy of the program text (replacing any earlier one). */
void diag_set_source(const char *src);

/* Prints one error. `kind` is "Parse" or "Semantic". col is counted from 1
 * and len is how many characters to underline; col 0 means "position
 * unknown", and then only the message line is printed. The source line is
 * left out when the line does not exist (for example the end of the file). */
void diag_print(FILE *out, const char *kind, int line, int col, int len, const char *msg);

#endif /* DIAG_H */
