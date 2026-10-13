/*
 * lexer.h : the interface of the lexer (also called the scanner).
 *
 * WHAT THE LEXER DOES
 * The source program is just one long string of characters. The lexer is the
 * first stage of the compiler: it reads that string and cuts it into TOKENS,
 * the smallest meaningful pieces. For example the line
 *
 *     C = A + [[1,2]]
 *
 * becomes the tokens   ID(C) EQUALS ID(A) PLUS LBRACKET LBRACKET NUMBER(1)
 *                      COMMA NUMBER(2) RBRACKET RBRACKET
 *
 * Spaces, tabs, new lines and # comments are skipped, they never become tokens.
 * The parser (parser.c) then reads these tokens instead of raw characters,
 * which makes its job much easier.
 *
 * HOW THE PARSER USES IT
 * The parser only ever needs to look ONE token ahead (the language is LL(1)),
 * so the lexer offers two calls: lex_peek() to look at the next token without
 * taking it, and lex_advance() to take it.
 */
#ifndef LEXER_H
#define LEXER_H

/* ---- Token types ----
 * Every token has exactly one of these kinds. The kinds cover: identifiers,
 * numbers, matrix brackets, the operators (+ - * =), the function keywords
 * (add / subtract / multiply / transpose / inverse), identity / zeros,
 * print / input / output, and the punctuation ( ) [ ] , ;
 */
typedef enum {
    TOK_EOF,        /* end of the program, no more tokens */
    TOK_ID,         /* a variable name such as A or total_2 */
    TOK_NUMBER,     /* 12 or 3.5 (a minus sign is a separate TOK_MINUS token) */
    TOK_DIMS,       /* a size like 2x3, used by input declarations */

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
    TOK_SEMICOLON,  /* ; (the language does not use it, but the lexer knows it) */

    /* keywords (function-call style). A keyword looks like a name but the
     * lexer recognises it and gives it its own kind, so it cannot be used as
     * a variable name. */
    TOK_ADD,
    TOK_SUBTRACT,
    TOK_MULTIPLY,
    TOK_TRANSPOSE,
    TOK_INVERSE,
    TOK_IDENTITY,   /* identity(n): the n x n identity matrix */
    TOK_ZEROS,      /* zeros(r, c): an r x c matrix of zeros */
    TOK_PRINT,

    /* keywords for programs that compile to a callable function */
    TOK_INPUT,
    TOK_OUTPUT,

    TOK_UNKNOWN     /* any character the language does not know, e.g. @ */
} TokenType;

/* Longest token text we keep (the rest is cut off). 63 characters + the
 * ending '\0'. Only matters for absurdly long names. */
#define TOKEN_TEXT_MAX 64

/* One token: what kind it is, the exact text, and where it sits in the source.
 * The position (line, col, len) is only used to print good error messages. */
typedef struct {
    TokenType type;
    char text[TOKEN_TEXT_MAX]; /* raw lexeme, e.g. "A", "12", "add" */
    int line;                  /* line number, counting from 1 */
    int col;                   /* column of its first character, counting from 1 */
    int len;                   /* how many characters it spans (0 for end of input) */
} Token;

/* Load source into the lexer. Must be called before peek/advance. */
void lex_init(const char *src);

/* Look at the current token without consuming it. Calling it again gives the
 * same token again. */
Token lex_peek(void);

/* Consume and return the current token, advancing to the next one. */
Token lex_advance(void);

/* Human-readable name for a token type, for debugging/printing. */
const char *token_type_name(TokenType type);

#endif /* LEXER_H */
