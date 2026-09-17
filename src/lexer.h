#ifndef LEXER_H
#define LEXER_H

/* ---- Token types ----
 * Covers: identifiers, numbers, matrix literals ([ ]),
 * infix operators (+ - * =), function-call keywords
 * (add/subtract/multiply/transpose/inverse), print,
 * and punctuation needed for argument lists (, ; ( )).
 */
typedef enum {
    TOK_EOF,
    TOK_ID,
    TOK_NUMBER,

    /* infix operators */
    TOK_PLUS,       /* + */
    TOK_MINUS,      /* - */
    TOK_STAR,       /* * */
    TOK_EQUALS,     /* = */

    /* punctuation */
    TOK_LPAREN,     /* ( */
    TOK_RPAREN,     /* ) */
    TOK_LBRACKET,   /* [ */
    TOK_RBRACKET,   /* ] */
    TOK_COMMA,      /* , */
    TOK_SEMICOLON,  /* ; */

    /* keywords (function-call style) */
    TOK_ADD,
    TOK_SUBTRACT,
    TOK_MULTIPLY,
    TOK_TRANSPOSE,
    TOK_INVERSE,
    TOK_PRINT,

    TOK_UNKNOWN
} TokenType;

#define TOKEN_TEXT_MAX 64

typedef struct {
    TokenType type;
    char text[TOKEN_TEXT_MAX]; /* raw lexeme, e.g. "A", "12", "add" */
    int line;
} Token;

/* Load source into the lexer. Must be called before peek/advance. */
void lex_init(const char *src);

/* Look at the current token without consuming it. */
Token lex_peek(void);

/* Consume and return the current token, advancing to the next one. */
Token lex_advance(void);

/* Human-readable name for a token type, for debugging/printing. */
const char *token_type_name(TokenType type);

#endif /* LEXER_H */