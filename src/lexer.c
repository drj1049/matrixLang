#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"

/* ---- Internal lexer state ---- */
static const char *src_ptr;   /* current read position in source */
static int current_line;
static const char *line_start; /* where the current line begins, to work out columns */
static Token cached_token;    /* one-token lookahead buffer */
static int has_cached;        /* whether cached_token is valid */

/* Keyword table: text the user types -> token type.
 * Checked after we've already scanned a whole identifier,
 * same approach the existing "print" handling already used.
 */
typedef struct {
    const char *word;
    TokenType type;
} Keyword;

static const Keyword KEYWORDS[] = {
    {"add",       TOK_ADD},
    {"subtract",  TOK_SUBTRACT},
    {"multiply",  TOK_MULTIPLY},
    {"transpose", TOK_TRANSPOSE},
    {"inverse",   TOK_INVERSE},
    {"identity",  TOK_IDENTITY},
    {"zeros",     TOK_ZEROS},
    {"print",     TOK_PRINT},
    {"input",     TOK_INPUT},
    {"output",    TOK_OUTPUT},
};
static const int NUM_KEYWORDS = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);

static TokenType keyword_lookup(const char *text) {
    for (int i = 0; i < NUM_KEYWORDS; i++) {
        if (strcmp(text, KEYWORDS[i].word) == 0) {
            return KEYWORDS[i].type;
        }
    }
    return TOK_ID; /* not a keyword -> plain identifier */
}

static void skip_whitespace_and_comments(void) {
    for (;;) {
        char c = *src_ptr;
        if (c == '\n') {
            current_line++;
            src_ptr++;
            line_start = src_ptr;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            src_ptr++;
        } else if (c == '#') {
            /* '#' starts a comment to end of line */
            while (*src_ptr != '\0' && *src_ptr != '\n') src_ptr++;
        } else {
            break;
        }
    }
}

static Token make_token(TokenType type, const char *start, int len) {
    Token t;
    t.type = type;
    t.line = current_line;
    t.col = (int)(start - line_start) + 1;
    t.len = len;
    if (len >= TOKEN_TEXT_MAX) len = TOKEN_TEXT_MAX - 1;
    memcpy(t.text, start, len);
    t.text[len] = '\0';
    return t;
}

static Token scan_identifier_or_keyword(void) {
    const char *start = src_ptr;
    while (isalnum((unsigned char)*src_ptr) || *src_ptr == '_') {
        src_ptr++;
    }
    int len = (int)(src_ptr - start);
    Token t = make_token(TOK_ID, start, len);
    t.type = keyword_lookup(t.text);
    return t;
}

static Token scan_number(void) {
    const char *start = src_ptr;
    while (isdigit((unsigned char)*src_ptr)) src_ptr++;
    /* digits 'x' digits is a matrix size, e.g. 2x3 */
    if (*src_ptr == 'x' && isdigit((unsigned char)src_ptr[1])) {
        src_ptr++;
        while (isdigit((unsigned char)*src_ptr)) src_ptr++;
        return make_token(TOK_DIMS, start, (int)(src_ptr - start));
    }
    if (*src_ptr == '.' && isdigit((unsigned char)src_ptr[1])) {
        src_ptr++;
        while (isdigit((unsigned char)*src_ptr)) src_ptr++;
    }
    int len = (int)(src_ptr - start);
    return make_token(TOK_NUMBER, start, len);
}

static Token scan_next_raw(void) {
    skip_whitespace_and_comments();

    if (*src_ptr == '\0') {
        return make_token(TOK_EOF, src_ptr, 0);
    }

    char c = *src_ptr;

    if (isalpha((unsigned char)c) || c == '_') {
        return scan_identifier_or_keyword();
    }
    if (isdigit((unsigned char)c)) {
        return scan_number();
    }

    /* single-character tokens */
    const char *start = src_ptr;
    src_ptr++;
    switch (c) {
        case '+': return make_token(TOK_PLUS, start, 1);
        case '-': return make_token(TOK_MINUS, start, 1);
        case '*': return make_token(TOK_STAR, start, 1);
        case '=': return make_token(TOK_EQUALS, start, 1);
        case '(': return make_token(TOK_LPAREN, start, 1);
        case ')': return make_token(TOK_RPAREN, start, 1);
        case '[': return make_token(TOK_LBRACKET, start, 1);
        case ']': return make_token(TOK_RBRACKET, start, 1);
        case ',': return make_token(TOK_COMMA, start, 1);
        case ';': return make_token(TOK_SEMICOLON, start, 1);
        default:
            /* keep a multi-byte character (UTF-8) together as one unknown token,
             * so an error message never shows half of a character */
            if ((unsigned char)c >= 0xC0) {
                while (((unsigned char)*src_ptr & 0xC0) == 0x80) src_ptr++;
            }
            return make_token(TOK_UNKNOWN, start, (int)(src_ptr - start));
    }
}

void lex_init(const char *src) {
    src_ptr = src;
    current_line = 1;
    line_start = src;
    has_cached = 0;
}

Token lex_peek(void) {
    if (!has_cached) {
        cached_token = scan_next_raw();
        has_cached = 1;
    }
    return cached_token;
}

Token lex_advance(void) {
    Token t = lex_peek();
    has_cached = 0;
    return t;
}

const char *token_type_name(TokenType type) {
    switch (type) {
        case TOK_EOF:        return "EOF";
        case TOK_ID:         return "ID";
        case TOK_NUMBER:     return "NUMBER";
        case TOK_DIMS:       return "DIMS";
        case TOK_PLUS:       return "PLUS";
        case TOK_MINUS:      return "MINUS";
        case TOK_STAR:       return "STAR";
        case TOK_EQUALS:     return "EQUALS";
        case TOK_LPAREN:     return "LPAREN";
        case TOK_RPAREN:     return "RPAREN";
        case TOK_LBRACKET:   return "LBRACKET";
        case TOK_RBRACKET:   return "RBRACKET";
        case TOK_COMMA:      return "COMMA";
        case TOK_SEMICOLON:  return "SEMICOLON";
        case TOK_ADD:        return "ADD";
        case TOK_SUBTRACT:   return "SUBTRACT";
        case TOK_MULTIPLY:   return "MULTIPLY";
        case TOK_TRANSPOSE:  return "TRANSPOSE";
        case TOK_INVERSE:    return "INVERSE";
        case TOK_IDENTITY:   return "IDENTITY";
        case TOK_ZEROS:      return "ZEROS";
        case TOK_PRINT:      return "PRINT";
        case TOK_INPUT:      return "INPUT";
        case TOK_OUTPUT:     return "OUTPUT";
        default:             return "UNKNOWN";
    }
}