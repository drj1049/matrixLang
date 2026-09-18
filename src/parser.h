#ifndef PARSER_H
#define PARSER_H

#include "ast.h"

/* A whole program is just a flat list of statements (ASSIGN or PRINT nodes),
 * in the order they appeared in the source.
 */
typedef struct {
    ASTNode **statements;
    int count;
} Program;

/* Tokenizes and parses the given source string into a Program.
 * Internally calls lex_init() on src, so the caller doesn't need to
 * touch the lexer directly. On a syntax error, prints a message to
 * stderr and exits -- matches how the lexer/earlier stages behave for now;
 * proper error recovery can be added later once the language is stable.
 */
Program parse_program(const char *src);

/* Frees every statement (and everything it owns) plus the statements array. */
void program_free(Program *prog);

#endif /* PARSER_H */