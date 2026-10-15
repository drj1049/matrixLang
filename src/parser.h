/*
 * parser.h : the interface of the parser (the second stage of the compiler).
 *
 * The parser takes the token stream from the lexer and checks that it follows
 * the grammar of MatrixLang. While checking, it builds the AST (see ast.h).
 * The technique used is RECURSIVE DESCENT: there is one C function for each
 * grammar rule, and the functions call each other the same way the rules
 * refer to each other. Because one token of lookahead is always enough to
 * choose the right rule, the grammar is LL(1).
 */
#ifndef PARSER_H
#define PARSER_H

#include "ast.h"

/* Most matrices one add(...), subtract(...) or multiply(...) call can take.
 * The parser rejects longer calls, because the TAC (tac.h) only has room for
 * this many names in one instruction. */
#define MAX_CALL_ARGS 16

/* A whole program is just a flat list of statements (ASSIGN, PRINT, INPUT
 * or OUTPUT nodes), in the order they appeared in the source.
 */
typedef struct {
    ASTNode **statements;  /* array of statement nodes */
    int count;             /* how many statements the array holds */
} Program;

/* Tokenizes and parses the given source string into a Program.
 * Internally calls lex_init() on src, so the caller doesn't need to
 * touch the lexer directly. A syntax error does not stop the parse: it is
 * recorded, the parser skips to the next statement and carries on, and at the
 * end every error found (up to 20) is printed to stderr, each as a
 * "Parse error at line N, column M: ..." line followed by the source line and
 * a caret (see diag.h), and the program exits(1). The text of src is copied
 * for diag.h, so the semantic checker can print carets later as well.
 */
Program parse_program(const char *src);

/* Frees every statement (and everything it owns) plus the statements array. */
void program_free(Program *prog);

#endif /* PARSER_H */
