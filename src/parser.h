#ifndef PARSER_H
#define PARSER_H

#include "ast.h"

/* A whole program is just a flat list of statements (ASSIGN, PRINT, INPUT
 * or OUTPUT nodes), in the order they appeared in the source.
 */
typedef struct {
    ASTNode **statements;
    int count;
} Program;

/* Tokenizes and parses the given source string into a Program.
 * Internally calls lex_init() on src, so the caller doesn't need to
 * touch the lexer directly. A syntax error does not stop the parse: it is
 * recorded, the parser skips to the next statement and carries on, and at the
 * end every error found (up to 20) is printed to stderr, one
 * "Parse error at line N: ..." line each, and the program exits(1).
 */
Program parse_program(const char *src);

/* Frees every statement (and everything it owns) plus the statements array. */
void program_free(Program *prog);

#endif /* PARSER_H */